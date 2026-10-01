#include <JuceHeader.h>
#include "../../Source/PluginSandboxCore/PluginWorkerMainCore.h"
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
