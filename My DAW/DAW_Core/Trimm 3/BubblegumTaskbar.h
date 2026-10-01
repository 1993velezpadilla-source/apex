#pragma once

/**
 * BubblegumTaskbar — master include for the Bubblegum taskbar family.
 *
 * Six focused nucleos plus this aggregator:
 *
 * Interface:
 *   IBubblegumTaskbarHost            — what panels implement to be minimizable
 *
 * Data / state:
 *   BubblegumTaskbarChipModel        — per-chip POD (host, bounds, flags)
 *   BubblegumTaskbarCore             — singleton manager + Listener pattern
 *
 * Logic:
 *   BubblegumTaskbarChipLayoutCore   — left-to-right layout based on label width
 *
 * Renderer:
 *   BubblegumTaskbarChipRenderer     — paints one chip, exposes close hit-test
 *
 * Component:
 *   BubblegumTaskbarComponent        — JUCE Component glue, lives in shell
 *
 * --- Lifecycle for a minimizable panel ---
 *
 *  class MyFloatingPanel : public juce::Component, public IBubblegumTaskbarHost
 *  {
 *  public:
 *      MyFloatingPanel(...) {
 *          BubblegumTaskbarCore::getGlobalInstance().registerHost(this);
 *      }
 *      ~MyFloatingPanel() override {
 *          BubblegumTaskbarCore::getGlobalInstance().unregisterHost(this);
 *      }
 *
 *      // From IBubblegumTaskbarHost:
 *      juce::String getChipLabel()        const override { return "Input trim gain ch 1"; }
 *      juce::Colour getChipAccentColour() const override { return juce::Colour(0xFFFF4F8A); }
 *      void restoreFromTaskbar() override {
 *          setVisible(true);
 *          BubblegumTaskbarCore::getGlobalInstance().setHostActive(this, true);
 *      }
 *      void minimizeFromTaskbar() override {
 *          setVisible(false);
 *          BubblegumTaskbarCore::getGlobalInstance().setHostActive(this, false);
 *      }
 *      void closeFromTaskbar() override {
 *          // panel destruction path — owner will delete the panel; the
 *          // dtor will call unregisterHost().
 *          if (auto* p = getParentComponent())
 *              p->postCommandMessage(kCloseSelf);
 *      }
 *  };
 */

#include "IBubblegumTaskbarHost.h"
#include "BubblegumTaskbarChipModel.h"
#include "BubblegumTaskbarChipLayoutCore.h"
#include "BubblegumTaskbarChipRenderer.h"
#include "BubblegumTaskbarCore.h"
#include "BubblegumTaskbarComponent.h"
