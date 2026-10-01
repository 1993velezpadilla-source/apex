// ===========================================================================
// TimePitchRenderCacheCore.h
// Cache de render para el sistema pitch/time.
//
// POR QUÉ EXISTE ESTE NÚCLEO:
//   Time stretch es una operación costosa.
//   No puedes ejecutar un phase vocoder completo en cada callback de audio.
//
//   Estrategia:
//     1. Audio thread: lee siempre desde buffer cacheado (lock-free)
//     2. Background thread: renderiza cuando el estado cambia
//     3. Cuando cualquier parámetro cambia → cache invalidada
//     4. Audio thread usa fallback simple mientras cache se regenera
//
//   Cache key: identifica unívocamente qué combinación de
//   pitch + stretch + formant + modo + source fue procesada.
//   Si la key cambia → la cache es inválida.
// ===========================================================================
#pragma once
#include "TimePitchTypesCore.h"
#include <JuceHeader.h>
#include <mutex>
#include <atomic>
#include <functional>

namespace ArrangementEditor
{

class TimePitchRenderCacheCore
{
public:
    TimePitchRenderCacheCore() = default;

    // -----------------------------------------------------------------------
    // Verifica si la cache es válida para la key dada
    // -----------------------------------------------------------------------
    bool isValid(const TimePitchCacheKey& key) const
    {
        if (!hasCache_.load()) return false;
        std::lock_guard<std::mutex> lock(mutex_);
        return currentKey_ == key;
    }

    // -----------------------------------------------------------------------
    // Devuelve el buffer cacheado (llamar solo si isValid() es true)
    // Para audio thread: usa tryLock, si falla usa fallback
    // -----------------------------------------------------------------------
    const juce::AudioBuffer<float>* tryGetBuffer() const
    {
        if (!hasCache_.load()) return nullptr;
        if (mutex_.try_lock())
        {
            mutex_.unlock();
            return &cachedBuffer_;
        }
        return nullptr; // audio thread no puede esperar
    }

    // -----------------------------------------------------------------------
    // Invalida la cache (llamar cuando el estado del clip cambia)
    // Seguro para llamar desde cualquier thread.
    // -----------------------------------------------------------------------
    void invalidate()
    {
        hasCache_.store(false);
    }

    // -----------------------------------------------------------------------
    // Almacena un nuevo buffer cacheado con su key.
    // Llamar desde background thread después del render.
    // -----------------------------------------------------------------------
    void store(const TimePitchCacheKey& key, juce::AudioBuffer<float>&& buffer)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        currentKey_   = key;
        cachedBuffer_ = std::move(buffer);
        hasCache_.store(true);
    }

    // -----------------------------------------------------------------------
    // Callback para solicitar render en background.
    // La implementación concreta lo conecta al ThreadPool del DAW.
    // -----------------------------------------------------------------------
    std::function<void(const TimePitchCacheKey&)> onCacheInvalidated;

    bool hasCachedData() const { return hasCache_.load(); }

private:
    mutable std::mutex         mutex_;
    juce::AudioBuffer<float>   cachedBuffer_;
    TimePitchCacheKey          currentKey_;
    std::atomic<bool>          hasCache_ { false };
};

} // namespace ArrangementEditor
