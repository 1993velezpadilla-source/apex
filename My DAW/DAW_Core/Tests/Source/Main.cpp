#include <JuceHeader.h>
#include "../../Source/PluginSandboxCore/PluginWorkerMainCore.h"
#include "../../Source/TuneLiveCore/ApexTuneLiveProcessor.h"
#include <fstream>
#include <iostream>

//==============================================================================
#define APEX_STRINGIFY_IMPL(x) #x
#define APEX_TO_STRING(x) APEX_STRINGIFY_IMPL(x)

//==============================================================================
class ConsoleLogger final : public juce::Logger
{
    void logMessage (const juce::String& message) override
    {
        std::cout << message << std::endl;
       #if JUCE_WINDOWS
        juce::Logger::outputDebugString (message);
       #endif
    }
};

//==============================================================================
class ConsoleUnitTestRunner final : public juce::UnitTestRunner
{
    void logMessage (const juce::String& message) override
    {
        juce::Logger::writeToLog (message);
    }
};

//==============================================================================
struct LoggerGuard
{
    ~LoggerGuard()
    {
        juce::Logger::setCurrentLogger (nullptr);
        juce::DeletedAtShutdown::deleteAll();
    }
};

//==============================================================================
int main (int argc, char** argv)
{
    constexpr auto helpOption     = "--help|-h";
    constexpr auto listOption     = "--list-categories|-l";
    constexpr auto categoryOption = "--category|-c";
    constexpr auto seedOption     = "--seed|-s";
    constexpr auto nameOption     = "--name|-n";
    constexpr auto resultsJsonOption = "--results-json";
    constexpr auto tuneLiveScreenshotOption = "--capture-tune-live-gui";

    constexpr juce::int64 defaultSeed = 0xA9E12026;

     try
     {
     juce::ArgumentList args (argc, argv);

     // The test executable is also the isolated hook-enabled worker artifact.
     // Dispatch happens before the unit-test logger/runner is created and
     // reuses the exact production PluginWorkerMainCore implementation.
     juce::StringArray processArguments;
     for (int i = 0; i < argc; ++i)
         processArguments.add (juce::String::fromUTF8 (argv[i]));
     const auto processMode =
         DAW::PluginSandboxCommandLineCore::resolveProcessMode (processArguments);
     DAW::ApexProcessModeStateCore::setProcessMode (processMode);
     if (processMode == DAW::ApexProcessMode::PluginWorker)
         return DAW::PluginWorkerMainCore::run (processArguments);

     if (args.containsOption (helpOption))
    {
        std::cout << argv[0]
                  << " [" << helpOption << "]"
                  << " [" << listOption << "]"
                  << " [" << categoryOption << "=category]"
                  << " [" << seedOption << "=seed]"
                  << " [" << nameOption << "=name]"
                  << " [" << resultsJsonOption << "=path]"
                  << std::endl;
        return 0;
    }

    if (args.containsOption (listOption))
    {
        for (auto& category : juce::UnitTest::getAllCategories())
            std::cout << category << std::endl;
        return 0;
    }

    // Keep one GUI lifetime across the entire run. Individual UI suites also
    // use ScopedJuceInitialiser_GUI; without this outer owner their teardown
    // deletes process-wide DeletedAtShutdown singletons (including Theme),
    // leaving later suites with dangling singleton references.
    juce::ScopedJuceInitialiser_GUI guiLifetime;

    // Explicit capture of the REAL APEX Tune Live JUCE editor. There is no
    // Photoshop/mock-up/hand-painted approximation here: this calls the
    // same production Processor::createEditor() used by the desktop host.
    // This is test-runner-only and never changes the actual DAW/plug-in.
    if (args.containsOption (tuneLiveScreenshotOption))
    {
        auto destination = args.getValueForOption (tuneLiveScreenshotOption);
        if (destination.isEmpty())
        {
            std::cerr << "Missing --capture-tune-live-gui=PATH.png" << std::endl;
            return 2;
        }
        APEX::TuneLive::Processor plugin;
        plugin.prepareToPlay (48000.0, 128);
        std::unique_ptr<juce::AudioProcessorEditor> editor (plugin.createEditor());
        if (! editor)
        {
            std::cerr << "Production Tune Live editor creation failed" << std::endl;
            return 1;
        }
        // Both Ab and Bb must be user-selectable WITHOUT changing existing
        // 12-tone key automation IDs/state. Exercise the REAL GUI ComboBox
        // callbacks and restore C before taking the visual snapshot.
        juce::ComboBox* keySelection = nullptr;
        for (int c=0;c<editor->getNumChildComponents();++c)
            if (auto* combo=dynamic_cast<juce::ComboBox*> (editor->getChildComponent (c)))
                if (combo->getNumItems()==17)
                    keySelection=combo;
        if (keySelection==nullptr)
        {
            std::cerr << "Expected 17 enharmonic key spellings in native GUI" << std::endl;
            return 1;
        }
        auto* keyParameter=plugin.getParameters()[APEX::TuneLive::Processor::kKey];
        keySelection->setSelectedId (13, juce::sendNotificationSync); // Ab / G#
        if (keySelection->getText()!="Ab"
            || std::abs (keyParameter->getValue()-8.0f/11.0f)>0.001f)
        {
            std::cerr << "Ab must map to existing G# key/semitone 8" << std::endl;
            return 1;
        }
        keySelection->setSelectedId (16, juce::sendNotificationSync); // Bb / A#
        if (keySelection->getText()!="Bb"
            || std::abs (keyParameter->getValue()-10.0f/11.0f)>0.001f)
        {
            std::cerr << "Bb must map to existing A# key/semitone 10" << std::endl;
            return 1;
        }
        keySelection->setSelectedId (1, juce::sendNotificationSync); // C
        if (std::abs (keyParameter->getValue())>0.001f)
            return 1;
        editor->setSize (1200, 750); // current APEX visual concept artboard
        const auto image = editor->createComponentSnapshot (
            editor->getLocalBounds(), true, 1.0f);
        if (! image.isValid() || image.getWidth() != 1200 || image.getHeight() != 750)
        {
            std::cerr << "Failed to render actual JUCE Tune Live component tree" << std::endl;
            return 1;
        }
        const juce::File output (destination);
        if (output.getParentDirectory().createDirectory().failed())
        {
            std::cerr << "Failed to create screenshot folder" << std::endl;
            return 1;
        }
        auto file = output.createOutputStream();
        juce::PNGImageFormat png;
        if (! file || ! png.writeImageToStream (image, *file))
        {
            std::cerr << "Failed to encode real APEX Tune Live screenshot" << std::endl;
            return 1;
        }
        file->flush();
        editor.reset();
        plugin.releaseResources();
        std::cout << "APEX_TUNE_LIVE_ACTUAL_JUCE_GUI_SCREENSHOT_PASS"
                  << " width=1200 height=750 enharmonic_Ab_Bb=PASS"
                  << " source=production_Processor_createEditor"
                  << " idle=true"
                  << " filename=" << output.getFullPathName().toStdString()
                  << std::endl;
        return 0;
    }

    ConsoleLogger logger;
    juce::Logger::setCurrentLogger (&logger);

    LoggerGuard onExit;

    ConsoleUnitTestRunner runner;
    runner.setAssertOnFailure (false);
    runner.setPassesAreLogged (false);

    const auto seed = [&]
    {
        if (args.containsOption (seedOption))
        {
            auto seedValueString = args.getValueForOption (seedOption);
            if (seedValueString.startsWith ("0x"))
                return seedValueString.getHexValue64();
            return seedValueString.getLargeIntValue();
        }
        return defaultSeed;
    }();

    if (args.containsOption (categoryOption))
        runner.runTestsInCategory (args.getValueForOption (categoryOption), seed);
    else if (args.containsOption (nameOption))
        runner.runTestsWithName (args.getValueForOption (nameOption), seed);
    else
        runner.runAllTests (seed);

    // Collect results
    int totalAssertionsPassed = 0;
    int totalAssertionsFailed = 0;
    int totalTestResults = runner.getNumResults();
    bool anyFailure = false;

    for (int i = 0; i < totalTestResults; ++i)
    {
        auto* result = runner.getResult (i);
        totalAssertionsPassed += result->passes;
        totalAssertionsFailed += result->failures;
        if (result->failures > 0)
            anyFailure = true;
    }

    // Console summary
    logger.writeToLog ("\n" + juce::String::repeatedString ("-", 65));

    for (int i = 0; i < totalTestResults; ++i)
    {
        auto* result = runner.getResult (i);
        if (result->failures > 0)
        {
            const auto testName = result->unitTestName + " / " + result->subcategoryName;
            const auto testSummary = juce::String (result->failures) + " failure"
                                     + (result->failures > 1 ? "s" : "");
            const auto nlTab = "\n\t";
            logger.writeToLog ("\n" + testName + ": " + testSummary + nlTab
                               + result->messages.joinIntoString (nlTab));
        }
    }

    if (anyFailure)
        logger.writeToLog ("Tests FAILED.");
    else if (totalTestResults > 0)
        logger.writeToLog ("All tests completed successfully.");

    // JSON output
    if (args.containsOption (resultsJsonOption))
    {
        auto jsonPath = args.getValueForOption (resultsJsonOption);

        juce::DynamicObject::Ptr root = new juce::DynamicObject();
        root->setProperty ("schemaVersion", 1);
        root->setProperty ("seed", seed);
       #ifdef APEX_TEST_CONFIGURATION
        root->setProperty ("configuration", juce::String (APEX_TO_STRING (APEX_TEST_CONFIGURATION)));
       #else
        root->setProperty ("configuration", juce::String ("Unknown"));
       #endif
        root->setProperty ("resultGroups", totalTestResults);
        root->setProperty ("assertionsPassed", totalAssertionsPassed);
        root->setProperty ("assertionsFailed", totalAssertionsFailed);
        root->setProperty ("durationMs", 0);

        juce::Array<juce::var> testsArray;
        for (int i = 0; i < totalTestResults; ++i)
        {
            auto* result = runner.getResult (i);
            juce::DynamicObject::Ptr testObj = new juce::DynamicObject();
            testObj->setProperty ("name", result->unitTestName);
            testObj->setProperty ("subcategory", result->subcategoryName);
            testObj->setProperty ("passes", result->passes);
            testObj->setProperty ("failures", result->failures);

            juce::Array<juce::var> messagesArray;
            for (const auto& msg : result->messages)
                messagesArray.add (msg);
            testObj->setProperty ("messages", juce::var (messagesArray));

            testsArray.add (juce::var (testObj.get()));
        }
        root->setProperty ("tests", juce::var (testsArray));

        juce::String jsonString = juce::JSON::toString (juce::var (root.get()), true);

        // Atomic write: write to temp then rename
        auto tempPath = jsonPath + ".tmp";
        std::ofstream ofs (tempPath.toRawUTF8());
        if (ofs.is_open())
        {
            ofs << jsonString.toStdString();
            ofs.close();
            juce::File (tempPath).moveFileTo (jsonPath);
        }
        else
        {
            logger.writeToLog ("Failed to write JSON results to: " + jsonPath);
        }
    }

    if (totalTestResults == 0)
        return 2;

    return anyFailure ? 1 : 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        std::cerr << "Usage: " << argv[0]
                  << " [" << helpOption << "]"
                  << " [" << listOption << "]"
                  << " [" << categoryOption << "=category]"
                  << " [" << seedOption << "=seed]"
                  << " [" << nameOption << "=name]"
                  << " [" << resultsJsonOption << "=path]"
                  << std::endl;
        return 2;
    }
}
