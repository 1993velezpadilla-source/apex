#pragma once

// Only the generated Android preview exporter adds juce_opengl. Desktop and
// the canonical Windows project retain their existing rendering backends.
#if JUCE_ANDROID && defined(APEX_ANDROID_GPU_UI) && APEX_ANDROID_GPU_UI
#include <juce_core/native/juce_JNIHelpers_android.h>
#include <android/log.h>
#include <atomic>
#include <functional>

namespace DAW
{
    inline juce::String androidLaunchExtra(const char* key)
    {
        auto* env = juce::getEnv();
        const auto activity = juce::getMainActivity();
        if (activity == nullptr)
            return {};
        const juce::LocalRef<jclass> cls(env->GetObjectClass(activity.get()));
        const auto getIntent = env->GetMethodID(cls.get(), "getIntent", "()Landroid/content/Intent;");
        const juce::LocalRef<jobject> intent(env->CallObjectMethod(activity.get(), getIntent));
        if (intent == nullptr)
            return {};
        const juce::LocalRef<jclass> intentClass(env->GetObjectClass(intent.get()));
        const auto getString = env->GetMethodID(intentClass.get(), "getStringExtra",
                                               "(Ljava/lang/String;)Ljava/lang/String;");
        const juce::LocalRef<jstring> name(env->NewStringUTF(key));
        const juce::LocalRef<jstring> value(static_cast<jstring>(
            env->CallObjectMethod(intent.get(), getString, name.get())));
        return value == nullptr ? juce::String() : juce::juceString(env, value.get());
    }

    // Used only by explicit adb benchmark launches, never by normal user flows.
    inline std::function<juce::String()> androidUiBenchmarkSnapshot;

    class AndroidGpuPresenter final : private juce::OpenGLRenderer,
                                      private juce::Timer
    {
    public:
        explicit AndroidGpuPresenter(juce::Component& target) : target_(&target)
        {
            if (androidLaunchExtra("apex_renderer") == "software")
            {
                backend_ = "software";
                __android_log_print(ANDROID_LOG_INFO, "APEX_RENDERER", "backend=software explicit_benchmark_override");
                return;
            }
            context_.setRenderer(this);
            context_.setComponentPaintingEnabled(true);
            context_.setContinuousRepainting(false);
            context_.setImageCacheSize(32u * 1024u * 1024u);
            context_.attachTo(target);
            // Failure must remain usable rather than leaving a black surface.
            // This one-shot check is control-thread-only, never audio work.
            startTimer(4000);
        }

        ~AndroidGpuPresenter() override
        {
            stopTimer();
            context_.detach(); // finish the render thread before owner/content teardown
            context_.setRenderer(nullptr);
        }

        juce::String backend() const { return backend_; }
        uint64_t renderCallbacks() const noexcept { return callbacks_.load(std::memory_order_relaxed); }
        bool ready() const noexcept { return ready_.load(std::memory_order_acquire); }

    private:
        void newOpenGLContextCreated() override
        {
            context_.setSwapInterval(1);
            ready_.store(true, std::memory_order_release);
            __android_log_print(ANDROID_LOG_INFO, "APEX_RENDERER", "backend=opengl context_ready=1");
        }

        void renderOpenGL() override
        {
            // JUCE paints the complete existing Component tree with its GL
            // graphics context after this callback. No separate/mock UI.
            juce::OpenGLHelpers::clear(juce::Colours::black);
            callbacks_.fetch_add(1, std::memory_order_relaxed);
        }

        void openGLContextClosing() override
        {
            ready_.store(false, std::memory_order_release);
        }

        void timerCallback() override
        {
            stopTimer();
            if (ready() && renderCallbacks() > 0)
                return;
            context_.detach();
            backend_ = "software-fallback";
            if (target_ != nullptr)
                target_->repaint();
            __android_log_print(ANDROID_LOG_ERROR, "APEX_RENDERER", "backend=software-fallback reason=no_gpu_frame");
        }

        juce::OpenGLContext context_;
        juce::Component::SafePointer<juce::Component> target_;
        std::atomic<bool> ready_{false};
        std::atomic<uint64_t> callbacks_{0};
        juce::String backend_{"opengl"};
    };
}
#endif
