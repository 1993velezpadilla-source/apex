#include "SafeAsioDeviceTypeCore.h"
#include <windows.h>
#include <limits>
#include <cstdlib>

namespace DAW::seh
{
static bool doOpen (juce::AudioIODevice* d,
                    const juce::BigInteger& in,
                    const juce::BigInteger& out,
                    double sr,
                    int buf,
                    juce::String& errOut) noexcept
{
    errOut = d->open (in, out, sr, buf);
    return true;
}

bool tryOpen (juce::AudioIODevice* d,
              const juce::BigInteger& in,
              const juce::BigInteger& out,
              double sr,
              int buf,
              juce::String& errOut) noexcept
{
    __try { return doOpen (d, in, out, sr, buf, errOut); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static void doStart (juce::AudioIODevice* d,
                     juce::AudioIODeviceCallback* cb) noexcept
{
    d->start (cb);
}

bool tryStart (juce::AudioIODevice* d,
               juce::AudioIODeviceCallback* cb) noexcept
{
    __try { doStart (d, cb); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static juce::StringArray doGetOutputChannelNames (juce::AudioIODevice* d) noexcept
{
    return d->getOutputChannelNames();
}

static juce::StringArray* doCreateOutputChannelNames (juce::AudioIODevice* d) noexcept
{
    return new juce::StringArray (doGetOutputChannelNames (d));
}

bool tryGetOutputChannelNames (juce::AudioIODevice* d,
                               juce::StringArray& namesOut) noexcept
{
    juce::StringArray* result = nullptr;
    __try { result = doCreateOutputChannelNames (d); }
    __except (EXCEPTION_EXECUTE_HANDLER) { result = nullptr; }

    if (result == nullptr)
    {
        namesOut.clear();
        return false;
    }

    namesOut = *result;
    delete result;
    return true;
}

static juce::StringArray doGetInputChannelNames (juce::AudioIODevice* d) noexcept
{
    return d->getInputChannelNames();
}

static juce::StringArray* doCreateInputChannelNames (juce::AudioIODevice* d) noexcept
{
    return new juce::StringArray (doGetInputChannelNames (d));
}

bool tryGetInputChannelNames (juce::AudioIODevice* d,
                              juce::StringArray& namesOut) noexcept
{
    juce::StringArray* result = nullptr;
    __try { result = doCreateInputChannelNames (d); }
    __except (EXCEPTION_EXECUTE_HANDLER) { result = nullptr; }

    if (result == nullptr)
    {
        namesOut.clear();
        return false;
    }

    namesOut = *result;
    delete result;
    return true;
}

static std::optional<juce::BigInteger> doGetDefaultOutputChannels (juce::AudioIODevice* d) noexcept
{
    return d->getDefaultOutputChannels();
}

static std::optional<juce::BigInteger>* doCreateDefaultOutputChannels (juce::AudioIODevice* d) noexcept
{
    return new std::optional<juce::BigInteger> (doGetDefaultOutputChannels (d));
}

bool tryGetDefaultOutputChannels (juce::AudioIODevice* d,
                                  std::optional<juce::BigInteger>& channelsOut) noexcept
{
    std::optional<juce::BigInteger>* result = nullptr;
    __try { result = doCreateDefaultOutputChannels (d); }
    __except (EXCEPTION_EXECUTE_HANDLER) { result = nullptr; }

    if (result == nullptr)
    {
        channelsOut.reset();
        return false;
    }

    channelsOut = *result;
    delete result;
    return true;
}

static std::optional<juce::BigInteger> doGetDefaultInputChannels (juce::AudioIODevice* d) noexcept
{
    return d->getDefaultInputChannels();
}

static std::optional<juce::BigInteger>* doCreateDefaultInputChannels (juce::AudioIODevice* d) noexcept
{
    return new std::optional<juce::BigInteger> (doGetDefaultInputChannels (d));
}

bool tryGetDefaultInputChannels (juce::AudioIODevice* d,
                                 std::optional<juce::BigInteger>& channelsOut) noexcept
{
    std::optional<juce::BigInteger>* result = nullptr;
    __try { result = doCreateDefaultInputChannels (d); }
    __except (EXCEPTION_EXECUTE_HANDLER) { result = nullptr; }

    if (result == nullptr)
    {
        channelsOut.reset();
        return false;
    }

    channelsOut = *result;
    delete result;
    return true;
}

static juce::Array<double> doGetAvailableSampleRates (juce::AudioIODevice* d) noexcept
{
    return d->getAvailableSampleRates();
}

static juce::Array<double>* doCreateAvailableSampleRates (juce::AudioIODevice* d) noexcept
{
    return new juce::Array<double> (doGetAvailableSampleRates (d));
}

bool tryGetAvailableSampleRates (juce::AudioIODevice* d,
                                 juce::Array<double>& ratesOut) noexcept
{
    juce::Array<double>* result = nullptr;
    __try { result = doCreateAvailableSampleRates (d); }
    __except (EXCEPTION_EXECUTE_HANDLER) { result = nullptr; }

    if (result == nullptr)
    {
        ratesOut.clear();
        return false;
    }

    ratesOut = *result;
    delete result;
    return true;
}

static juce::Array<int> doGetAvailableBufferSizes (juce::AudioIODevice* d) noexcept
{
    return d->getAvailableBufferSizes();
}

static juce::Array<int>* doCreateAvailableBufferSizes (juce::AudioIODevice* d) noexcept
{
    return new juce::Array<int> (doGetAvailableBufferSizes (d));
}

bool tryGetAvailableBufferSizes (juce::AudioIODevice* d,
                                 juce::Array<int>& sizesOut) noexcept
{
    juce::Array<int>* result = nullptr;
    __try { result = doCreateAvailableBufferSizes (d); }
    __except (EXCEPTION_EXECUTE_HANDLER) { result = nullptr; }

    if (result == nullptr)
    {
        sizesOut.clear();
        return false;
    }

    sizesOut = *result;
    delete result;
    return true;
}

static int doGetDefaultBufferSize (juce::AudioIODevice* d) noexcept
{
    return d->getDefaultBufferSize();
}

bool tryGetDefaultBufferSize (juce::AudioIODevice* d,
                              int& sizeOut) noexcept
{
    __try
    {
        sizeOut = doGetDefaultBufferSize (d);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        sizeOut = 0;
        return false;
    }
}

static bool doIsOpen (juce::AudioIODevice* d) noexcept
{
    return d->isOpen();
}

bool tryIsOpen (juce::AudioIODevice* d,
                bool& isOpenOut) noexcept
{
    __try
    {
        isOpenOut = doIsOpen (d);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        isOpenOut = false;
        return false;
    }
}

static double doGetCurrentSampleRate (juce::AudioIODevice* d) noexcept
{
    return d->getCurrentSampleRate();
}

bool tryGetCurrentSampleRate (juce::AudioIODevice* d,
                              double& sampleRateOut) noexcept
{
    __try
    {
        sampleRateOut = doGetCurrentSampleRate (d);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        sampleRateOut = 0.0;
        return false;
    }
}

static int doGetCurrentBufferSizeSamples (juce::AudioIODevice* d) noexcept
{
    return d->getCurrentBufferSizeSamples();
}

bool tryGetCurrentBufferSizeSamples (juce::AudioIODevice* d,
                                     int& bufferSizeOut) noexcept
{
    __try
    {
        bufferSizeOut = doGetCurrentBufferSizeSamples (d);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        bufferSizeOut = 0;
        return false;
    }
}

static juce::BigInteger doGetActiveInputChannels (juce::AudioIODevice* d) noexcept
{
    return d->getActiveInputChannels();
}

static juce::BigInteger* doCreateActiveInputChannels (juce::AudioIODevice* d) noexcept
{
    return new juce::BigInteger (doGetActiveInputChannels (d));
}

bool tryGetActiveInputChannels (juce::AudioIODevice* d,
                                juce::BigInteger& channelsOut) noexcept
{
    juce::BigInteger* result = nullptr;
    __try { result = doCreateActiveInputChannels (d); }
    __except (EXCEPTION_EXECUTE_HANDLER) { result = nullptr; }

    if (result == nullptr)
    {
        channelsOut.clear();
        return false;
    }

    channelsOut = *result;
    delete result;
    return true;
}

static juce::BigInteger doGetActiveOutputChannels (juce::AudioIODevice* d) noexcept
{
    return d->getActiveOutputChannels();
}

static juce::BigInteger* doCreateActiveOutputChannels (juce::AudioIODevice* d) noexcept
{
    return new juce::BigInteger (doGetActiveOutputChannels (d));
}

bool tryGetActiveOutputChannels (juce::AudioIODevice* d,
                                 juce::BigInteger& channelsOut) noexcept
{
    juce::BigInteger* result = nullptr;
    __try { result = doCreateActiveOutputChannels (d); }
    __except (EXCEPTION_EXECUTE_HANDLER) { result = nullptr; }

    if (result == nullptr)
    {
        channelsOut.clear();
        return false;
    }

    channelsOut = *result;
    delete result;
    return true;
}

static juce::AudioIODevice* doCreate (juce::AudioIODeviceType* t,
                                      const juce::String& o,
                                      const juce::String& i) noexcept
{
    return t->createDevice (o, i);
}

juce::AudioIODevice* tryCreate (juce::AudioIODeviceType* t,
                                const juce::String& o,
                                const juce::String& i) noexcept
{
    __try { return doCreate (t, o, i); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

static void doScan (juce::AudioIODeviceType* inner) noexcept
{
    inner->scanForDevices();
}

static bool tryScan (juce::AudioIODeviceType* inner) noexcept
{
    __try { doScan (inner); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static juce::StringArray* doCreateDeviceNames (juce::AudioIODeviceType* t,
                                               bool wantInputNames) noexcept
{
    return new juce::StringArray (t->getDeviceNames (wantInputNames));
}

bool tryGetDeviceNames (juce::AudioIODeviceType* t,
                        bool wantInputNames,
                        juce::StringArray& namesOut) noexcept
{
    juce::StringArray* result = nullptr;
    __try { result = doCreateDeviceNames (t, wantInputNames); }
    __except (EXCEPTION_EXECUTE_HANDLER) { result = nullptr; }

    if (result == nullptr)
    {
        namesOut.clear();
        return false;
    }

    namesOut = *result;
    delete result;
    return true;
}

static int doGetDefaultDeviceIndex (juce::AudioIODeviceType* t, bool forInput) noexcept
{
    return t->getDefaultDeviceIndex (forInput);
}

bool tryGetDefaultDeviceIndex (juce::AudioIODeviceType* t,
                               bool forInput,
                               int& indexOut) noexcept
{
    __try
    {
        indexOut = doGetDefaultDeviceIndex (t, forInput);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        indexOut = -1;
        return false;
    }
}

static int doGetIndexOfDevice (juce::AudioIODeviceType* t,
                               juce::AudioIODevice* d,
                               bool asInput) noexcept
{
    return t->getIndexOfDevice (d, asInput);
}

bool tryGetIndexOfDevice (juce::AudioIODeviceType* t,
                          juce::AudioIODevice* d,
                          bool asInput,
                          int& indexOut) noexcept
{
    __try
    {
        indexOut = doGetIndexOfDevice (t, d, asInput);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        indexOut = -1;
        return false;
    }
}

static bool doHasSeparateInputsAndOutputs (juce::AudioIODeviceType* t) noexcept
{
    return t->hasSeparateInputsAndOutputs();
}

bool tryHasSeparateInputsAndOutputs (juce::AudioIODeviceType* t,
                                     bool& resultOut) noexcept
{
    __try
    {
        resultOut = doHasSeparateInputsAndOutputs (t);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        resultOut = false;
        return false;
    }
}
}

namespace DAW {

namespace
{
    static constexpr double kFallbackSampleRate = 48000.0;
    static constexpr int kFallbackBufferSize = 512;

    double sanitiseSampleRate (const juce::Array<double>& availableRates, double requested)
    {
        if (availableRates.isEmpty())
            return requested > 0.0 ? requested : kFallbackSampleRate;

        double bestRate = 0.0;
        double bestDistance = (std::numeric_limits<double>::max)();
        const double targetRate = requested > 0.0 ? requested : kFallbackSampleRate;

        for (auto rate : availableRates)
        {
            if (rate <= 0.0)
                continue;

            const double distance = std::abs (rate - targetRate);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                bestRate = rate;
            }
        }

        return bestRate > 0.0 ? bestRate : kFallbackSampleRate;
    }

    int sanitiseBufferSize (const juce::Array<int>& availableSizes,
                            int defaultBufferSize,
                            int requested)
    {
        if (availableSizes.isEmpty())
            return requested > 0 ? requested : juce::jmax (defaultBufferSize, kFallbackBufferSize);

        int bestSize = 0;
        int bestDistance = (std::numeric_limits<int>::max)();
        const int defaultSize = juce::jmax (defaultBufferSize, kFallbackBufferSize);
        const int targetSize = requested > 0 ? requested : defaultSize;

        for (auto size : availableSizes)
        {
            if (size <= 0)
                continue;

            const int distance = std::abs (size - targetSize);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                bestSize = size;
            }
        }

        return bestSize > 0 ? bestSize : defaultSize;
    }

    juce::String validateOpenedAsioState (juce::AudioIODevice& device, bool* driverCrashedOut = nullptr)
    {
        auto flagCrash = [driverCrashedOut]
        {
            if (driverCrashedOut != nullptr)
                *driverCrashedOut = true;
        };

        if (driverCrashedOut != nullptr)
            *driverCrashedOut = false;

        bool isOpen = false;
        if (! seh::tryIsOpen (&device, isOpen))
        {
            flagCrash();
            return "ASIO device crashed while checking open state";
        }

        if (! isOpen)
            return "ASIO device did not open";

        double currentSampleRate = 0.0;
        if (! seh::tryGetCurrentSampleRate (&device, currentSampleRate))
        {
            flagCrash();
            return "ASIO device crashed while reading sample rate after open";
        }

        if (currentSampleRate <= 0.0)
            return "ASIO device reported an invalid sample rate after open";

        int currentBufferSize = 0;
        if (! seh::tryGetCurrentBufferSizeSamples (&device, currentBufferSize))
        {
            flagCrash();
            return "ASIO device crashed while reading buffer size after open";
        }

        if (currentBufferSize <= 0)
            return "ASIO device reported an invalid buffer size after open";

        juce::BigInteger activeInputChannels;
        if (! seh::tryGetActiveInputChannels (&device, activeInputChannels))
        {
            flagCrash();
            return "ASIO device crashed while reading active input channels after open";
        }

        juce::BigInteger activeOutputChannels;
        if (! seh::tryGetActiveOutputChannels (&device, activeOutputChannels))
        {
            flagCrash();
            return "ASIO device crashed while reading active output channels after open";
        }

        juce::StringArray outputChannelNames;
        if (! seh::tryGetOutputChannelNames (&device, outputChannelNames))
        {
            flagCrash();
            return "ASIO device crashed while reading output channels after open";
        }

        const auto totalActiveChannels = activeInputChannels.countNumberOfSetBits()
                                       + activeOutputChannels.countNumberOfSetBits();

        if (totalActiveChannels <= 0)
            return "ASIO device reported no active channels after open";

        if (! outputChannelNames.isEmpty() && activeOutputChannels.countNumberOfSetBits() <= 0)
            return "ASIO device reported no active output channels after open";

        DBG("[SafeAsioDeviceType] opened device=" + device.getName()
            + " activeOutBits=" + activeOutputChannels.toString(2)
            + " activeInBits=" + activeInputChannels.toString(2)
            + " outputNames=" + outputChannelNames.joinIntoString(", ")
            + " sampleRate=" + juce::String(currentSampleRate)
            + " bufferSize=" + juce::String(currentBufferSize));

        return {};
    }
}

GuardedAsioDevice::GuardedAsioDevice (std::unique_ptr<juce::AudioIODevice> inner)
    : juce::AudioIODevice (inner != nullptr ? inner->getName() : juce::String(),
                           inner != nullptr ? inner->getTypeName() : juce::String("ASIO")),
      inner_ (std::move (inner))
{
}

juce::StringArray GuardedAsioDevice::getOutputChannelNames()
{
    juce::StringArray names;
    if (inner_ != nullptr)
        seh::tryGetOutputChannelNames (inner_.get(), names);
    return names;
}

juce::StringArray GuardedAsioDevice::getInputChannelNames()
{
    juce::StringArray names;
    if (inner_ != nullptr)
        seh::tryGetInputChannelNames (inner_.get(), names);
    return names;
}

std::optional<juce::BigInteger> GuardedAsioDevice::getDefaultOutputChannels() const
{
    std::optional<juce::BigInteger> channels;
    if (inner_ != nullptr)
        seh::tryGetDefaultOutputChannels (inner_.get(), channels);
    return channels;
}

std::optional<juce::BigInteger> GuardedAsioDevice::getDefaultInputChannels() const
{
    std::optional<juce::BigInteger> channels;
    if (inner_ != nullptr)
        seh::tryGetDefaultInputChannels (inner_.get(), channels);
    return channels;
}

juce::Array<double> GuardedAsioDevice::getAvailableSampleRates()
{
    juce::Array<double> rates;
    if (inner_ != nullptr)
        seh::tryGetAvailableSampleRates (inner_.get(), rates);
    return rates;
}

juce::Array<int> GuardedAsioDevice::getAvailableBufferSizes()
{
    juce::Array<int> sizes;
    if (inner_ != nullptr)
        seh::tryGetAvailableBufferSizes (inner_.get(), sizes);
    return sizes;
}

int GuardedAsioDevice::getDefaultBufferSize()
{
    int size = 0;
    if (inner_ != nullptr)
        seh::tryGetDefaultBufferSize (inner_.get(), size);
    return size;
}

juce::String GuardedAsioDevice::open (const juce::BigInteger& inputChannels,
                                      const juce::BigInteger& outputChannels,
                                      double sampleRate,
                                      int bufferSizeSamples)
{
    juce::String err;

    if (inner_ == nullptr)
        err = "ASIO device wrapper has no inner device";
    else
    {
        std::optional<juce::BigInteger> defaultInputChannels;
        std::optional<juce::BigInteger> defaultOutputChannels;
        juce::StringArray inputChannelNames;
        juce::StringArray outputChannelNames;
        juce::Array<double> availableSampleRates;
        juce::Array<int> availableBufferSizes;
        int defaultBufferSize = 0;

        if (! seh::tryGetDefaultInputChannels (inner_.get(), defaultInputChannels))
            err = "ASIO driver crashed while querying default input channels";
        else if (! seh::tryGetDefaultOutputChannels (inner_.get(), defaultOutputChannels))
            err = "ASIO driver crashed while querying default output channels";
        else if (! seh::tryGetInputChannelNames (inner_.get(), inputChannelNames))
            err = "ASIO driver crashed while querying input channels";
        else if (! seh::tryGetOutputChannelNames (inner_.get(), outputChannelNames))
            err = "ASIO driver crashed while querying output channels";
        else if (! seh::tryGetAvailableSampleRates (inner_.get(), availableSampleRates))
            err = "ASIO driver crashed while querying sample rates";
        else if (! seh::tryGetAvailableBufferSizes (inner_.get(), availableBufferSizes))
            err = "ASIO driver crashed while querying buffer sizes";
        else if (! seh::tryGetDefaultBufferSize (inner_.get(), defaultBufferSize))
            err = "ASIO driver crashed while querying default buffer size";

        auto sanitiseChannelsForOpen = [] (const juce::BigInteger& requested,
                                           const std::optional<juce::BigInteger>& defaultChannels,
                                           int availableChannels,
                                           bool preferDefaultWhenRequestedEmpty)
        {
            juce::BigInteger valid;

            auto clampToAvailable = [availableChannels] (const juce::BigInteger& source)
            {
                juce::BigInteger clipped;

                for (int bit = source.findNextSetBit (0); bit >= 0; bit = source.findNextSetBit (bit + 1))
                    if (bit < availableChannels)
                        clipped.setBit (bit);

                return clipped;
            };

            if (availableChannels <= 0)
                return valid;

            valid = clampToAvailable (requested);
            if (valid.countNumberOfSetBits() > 0)
                return valid;

            if (requested.countNumberOfSetBits() == 0 && ! preferDefaultWhenRequestedEmpty)
                return valid;

            if (defaultChannels.has_value())
            {
                valid = clampToAvailable (*defaultChannels);
                if (valid.countNumberOfSetBits() > 0)
                    return valid;
            }

            const int fallbackCount = juce::jmin (2, availableChannels);
            for (int channel = 0; channel < fallbackCount; ++channel)
                valid.setBit (channel);

            return valid;
        };

        auto safeInputChannels = sanitiseChannelsForOpen (inputChannels,
                                                          defaultInputChannels,
                                                          inputChannelNames.size(),
                                                          false);
        auto safeOutputChannels = sanitiseChannelsForOpen (outputChannels,
                                                           defaultOutputChannels,
                                                           outputChannelNames.size(),
                                                           true);

        if (err.isEmpty() && safeOutputChannels.countNumberOfSetBits() <= 0)
            err = "ASIO device has no usable output channels";
        else if (err.isEmpty() && safeInputChannels.countNumberOfSetBits() + safeOutputChannels.countNumberOfSetBits() <= 0)
            err = "ASIO device has no usable input or output channels";
        else if (err.isEmpty())
        {
            sampleRate = sanitiseSampleRate (availableSampleRates, sampleRate);
            bufferSizeSamples = sanitiseBufferSize (availableBufferSizes, defaultBufferSize, bufferSizeSamples);

            if (! seh::tryOpen (inner_.get(), safeInputChannels, safeOutputChannels, sampleRate, bufferSizeSamples, err))
                err = "ASIO driver crashed during open (guarded)";
            else if (err.isEmpty())
            {
                // Distinguish a crashing driver (fatal: close it) from a
                // driver that merely reports transient state right after
                // open (ASIO4ALL settles its WDM endpoints asynchronously).
                // Closing an actually-working device here caused the
                // "open but no playback/record/monitor" failure mode.
                bool driverCrashed = false;
                const auto stateReport = validateOpenedAsioState (*inner_, &driverCrashed);

                if (driverCrashed)
                    err = stateReport;
                else if (stateReport.isNotEmpty())
                    DBG("[SafeAsioDeviceType] open() advisory (keeping device open): " + stateReport);
            }

            if (err.isNotEmpty() && inner_->isOpen())
                inner_->close();
        }
    }

    lastError_ = err;

    if (lastError_.isNotEmpty())
        DBG("[SafeAsioDeviceType] " + lastError_);

    return err;
}

void GuardedAsioDevice::close()
{
    if (inner_ != nullptr)
        inner_->close();
}

bool GuardedAsioDevice::isOpen()
{
    bool open = false;
    if (inner_ != nullptr)
        seh::tryIsOpen (inner_.get(), open);
    return open;
}

void GuardedAsioDevice::start (juce::AudioIODeviceCallback* callback)
{
    if (inner_ == nullptr)
    {
        lastError_ = "ASIO device wrapper has no inner device";
        DBG("[SafeAsioDeviceType] " + lastError_);
        return;
    }

    if (callback == nullptr)
    {
        lastError_ = "ASIO start requested with a null callback";
        DBG("[SafeAsioDeviceType] " + lastError_);
        return;
    }

    // Pre-validation is ADVISORY only.  ASIO4ALL often reports transient
    // state (empty active channels / zero rate) for a moment after open()
    // while its WDM endpoints settle.  Refusing to start here is a silent
    // failure: JUCE's AudioDeviceManager never checks whether start()
    // succeeded, so the session looks healthy but never delivers a single
    // callback - no playback, no recording, no monitoring.  The start call
    // itself is SEH-guarded, so attempting it is always safe.  Only a
    // genuinely un-open device is a hard block.
    bool innerIsOpen = false;
    if (! seh::tryIsOpen (inner_.get(), innerIsOpen) || ! innerIsOpen)
    {
        lastError_ = "ASIO start requested on a device that is not open";
        DBG("[SafeAsioDeviceType] " + lastError_);
        return;
    }

    const auto advisory = validateOpenedAsioState (*inner_);
    if (advisory.isNotEmpty())
        DBG("[SafeAsioDeviceType] start() advisory (continuing anyway): " + advisory);

    if (! seh::tryStart (inner_.get(), callback))
    {
        lastError_ = "ASIO driver crashed during start (guarded)";
        DBG("[SafeAsioDeviceType] " + lastError_);
        return;
    }

    lastError_.clear();
}

void GuardedAsioDevice::stop()
{
    if (inner_ != nullptr)
        inner_->stop();
}

bool GuardedAsioDevice::isPlaying()
{
    return inner_ != nullptr && inner_->isPlaying();
}

juce::String GuardedAsioDevice::getLastError()
{
    return lastError_.isNotEmpty() ? lastError_ : (inner_ != nullptr ? inner_->getLastError() : juce::String());
}

int GuardedAsioDevice::getCurrentBufferSizeSamples()
{
    return inner_ != nullptr ? inner_->getCurrentBufferSizeSamples() : 0;
}

double GuardedAsioDevice::getCurrentSampleRate()
{
    return inner_ != nullptr ? inner_->getCurrentSampleRate() : 0.0;
}

int GuardedAsioDevice::getCurrentBitDepth()
{
    return inner_ != nullptr ? inner_->getCurrentBitDepth() : 0;
}

juce::BigInteger GuardedAsioDevice::getActiveOutputChannels() const
{
    juce::BigInteger channels;
    if (inner_ != nullptr)
        seh::tryGetActiveOutputChannels (inner_.get(), channels);
    return channels;
}

juce::BigInteger GuardedAsioDevice::getActiveInputChannels() const
{
    juce::BigInteger channels;
    if (inner_ != nullptr)
        seh::tryGetActiveInputChannels (inner_.get(), channels);
    return channels;
}

int GuardedAsioDevice::getOutputLatencyInSamples()
{
    return inner_ != nullptr ? inner_->getOutputLatencyInSamples() : 0;
}

int GuardedAsioDevice::getInputLatencyInSamples()
{
    return inner_ != nullptr ? inner_->getInputLatencyInSamples() : 0;
}

juce::AudioWorkgroup GuardedAsioDevice::getWorkgroup() const
{
    return inner_ != nullptr ? inner_->getWorkgroup() : juce::AudioWorkgroup{};
}

bool GuardedAsioDevice::hasControlPanel() const
{
    return inner_ != nullptr && inner_->hasControlPanel();
}

bool GuardedAsioDevice::showControlPanel()
{
    return inner_ != nullptr && inner_->showControlPanel();
}

bool GuardedAsioDevice::setAudioPreprocessingEnabled (bool shouldBeEnabled)
{
    return inner_ != nullptr && inner_->setAudioPreprocessingEnabled (shouldBeEnabled);
}

int GuardedAsioDevice::getXRunCount() const noexcept
{
    return inner_ != nullptr ? inner_->getXRunCount() : -1;
}

SafeAsioDeviceType::SafeAsioDeviceType()
    : juce::AudioIODeviceType("ASIO")
{
    inner_.reset(juce::AudioIODeviceType::createAudioIODeviceType_ASIO());
    if (inner_ != nullptr)
        inner_->addListener(this);
}

SafeAsioDeviceType::~SafeAsioDeviceType()
{
    if (inner_ != nullptr)
        inner_->removeListener(this);
}

juce::StringArray SafeAsioDeviceType::getDeviceNames (bool wantInputNames) const
{
    DBG ("[SafeAsioDeviceType] getDeviceNames() ENTERED, wantInput=" + juce::String (wantInputNames ? 1 : 0));

    juce::StringArray names;
    if (inner_ != nullptr)
        if (! seh::tryGetDeviceNames (inner_.get(), wantInputNames, names))
            DBG ("[SafeAsioDeviceType] getDeviceNames trapped an SEH exception");

    return names;
}

int SafeAsioDeviceType::getDefaultDeviceIndex (bool forInput) const
{
    int index = -1;
    if (inner_ != nullptr)
        if (! seh::tryGetDefaultDeviceIndex (inner_.get(), forInput, index))
            DBG ("[SafeAsioDeviceType] getDefaultDeviceIndex trapped an SEH exception");

    juce::StringArray names;
    if (inner_ != nullptr && seh::tryGetDeviceNames (inner_.get(), forInput, names))
    {
        if (index >= 0 && index < names.size())
            return index;

        return names.isEmpty() ? -1 : 0;
    }

    return index;
}

int SafeAsioDeviceType::getIndexOfDevice (juce::AudioIODevice* d, bool asInput) const
{
    int index = -1;
    if (inner_ != nullptr)
        if (! seh::tryGetIndexOfDevice (inner_.get(), d, asInput, index))
            DBG ("[SafeAsioDeviceType] getIndexOfDevice trapped an SEH exception");
    return index;
}

bool SafeAsioDeviceType::hasSeparateInputsAndOutputs() const
{
    bool result = false;
    if (inner_ != nullptr)
        if (! seh::tryHasSeparateInputsAndOutputs (inner_.get(), result))
            DBG ("[SafeAsioDeviceType] hasSeparateInputsAndOutputs trapped an SEH exception");
    return result;
}

void SafeAsioDeviceType::scanForDevices()
{
    DBG ("[SafeAsioDeviceType] scanForDevices() ENTERED");

    if (inner_ != nullptr)
        if (!seh::tryScan(inner_.get()))
            DBG("[SafeAsioDeviceType] scanForDevices trapped an SEH exception");
}

juce::AudioIODevice* SafeAsioDeviceType::createDevice (const juce::String& outputDeviceName,
                                                       const juce::String& inputDeviceName)
{
    DBG ("[SafeAsioDeviceType] createDevice() ENTERED out=" + outputDeviceName);

    if (inner_ == nullptr)
        return nullptr;

    juce::StringArray availableOutputs;
    if (! seh::tryGetDeviceNames (inner_.get(), false, availableOutputs))
    {
        DBG("[SafeAsioDeviceType] Refusing to create ASIO device after output device enumeration trapped an SEH exception");
        return nullptr;
    }

    juce::String requestedOutput = outputDeviceName.trim();
    if (requestedOutput.isEmpty() || requestedOutput.containsIgnoreCase("none"))
    {
        int defaultOutputIndex = -1;
        if (seh::tryGetDefaultDeviceIndex (inner_.get(), false, defaultOutputIndex)
            && juce::isPositiveAndBelow (defaultOutputIndex, availableOutputs.size()))
        {
            requestedOutput = availableOutputs[defaultOutputIndex];
            DBG("[SafeAsioDeviceType] Resolved missing ASIO output device to default: " + requestedOutput);
        }
        else if (! availableOutputs.isEmpty())
        {
            requestedOutput = availableOutputs[0];
            DBG("[SafeAsioDeviceType] Resolved missing ASIO output device to first available: " + requestedOutput);
        }
    }

    if (requestedOutput.isEmpty() || !availableOutputs.contains(requestedOutput))
    {
        DBG("[SafeAsioDeviceType] Refusing to create ASIO device for unavailable output device: " + requestedOutput);
        return nullptr;
    }

    auto requestedInput = inputDeviceName.trim();
    juce::StringArray availableInputs;
    if (! seh::tryGetDeviceNames (inner_.get(), true, availableInputs))
    {
        DBG("[SafeAsioDeviceType] Clearing ASIO input device after input device enumeration trapped an SEH exception");
        availableInputs.clear();
    }

    bool hasSeparateInputsAndOutputs = false;
    if (! seh::tryHasSeparateInputsAndOutputs (inner_.get(), hasSeparateInputsAndOutputs))
        hasSeparateInputsAndOutputs = false;

    if (! hasSeparateInputsAndOutputs
        && requestedInput.isEmpty()
        && availableInputs.contains(requestedOutput))
    {
        requestedInput = requestedOutput;
    }

    if (requestedInput.isNotEmpty() && !availableInputs.contains(requestedInput))
        requestedInput.clear();

    auto* raw = seh::tryCreate (inner_.get(), requestedOutput, requestedInput);

    if (raw == nullptr)
        return nullptr;

    return new GuardedAsioDevice (std::unique_ptr<juce::AudioIODevice> (raw));
}

void installSafeAudioDeviceTypes (juce::AudioDeviceManager& dm)
{
    auto add = [&dm](juce::AudioIODeviceType* t)
    {
        if (t != nullptr)
            dm.addAudioDeviceType (std::unique_ptr<juce::AudioIODeviceType> (t));
    };

    add (juce::AudioIODeviceType::createAudioIODeviceType_WASAPI (juce::WASAPIDeviceMode::shared));
    add (juce::AudioIODeviceType::createAudioIODeviceType_WASAPI (juce::WASAPIDeviceMode::exclusive));
    add (juce::AudioIODeviceType::createAudioIODeviceType_DirectSound());

    auto safeAsio = std::make_unique<DAW::SafeAsioDeviceType>();
    const bool asioUsable = safeAsio->isUsable();
    if (asioUsable)
        dm.addAudioDeviceType (std::move (safeAsio));

    DBG ("[SafeAsioDeviceType] Registered device types:");
    int asioCount = 0;
    for (auto* t : dm.getAvailableDeviceTypes())
    {
        if (t == nullptr)
            continue;

        DBG ("    type: " + t->getTypeName());
        if (t->getTypeName() == "ASIO")
            ++asioCount;
    }

    DBG ("[SafeAsioDeviceType] ASIO entry count = " + juce::String (asioCount)
         + "  (MUST be 1)");
}

} // namespace DAW
