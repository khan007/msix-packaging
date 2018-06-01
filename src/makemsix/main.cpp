//
//  Copyright (C) 2017 Microsoft.  All rights reserved.
//  See LICENSE file in the project root for full license information.
// 
#include "MSIXWindows.hpp"
#include "AppxPackaging.hpp"

#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <initializer_list>
#include <algorithm>

// Describes which command the user specified
enum class UserSpecified
{
    Nothing,
    Help,
    Unpack
};

// Tracks the state of the current parse operation as well as implements input validation
struct State
{
    bool Specify(UserSpecified spec)
    {
        if (specified != UserSpecified::Nothing || spec == UserSpecified::Help)
        {
            specified = UserSpecified::Help; // Because clearly the user needs some
            return false;
        }
        specified = spec;
        return true;
    }

    bool SkipManifestValidation()
    {
        validationOptions = static_cast<MSIX_VALIDATION_OPTION>(validationOptions | MSIX_VALIDATION_OPTION::MSIX_VALIDATION_OPTION_SKIPAPPXMANIFEST);
        return true;
    }

    bool SkipSignature()
    {
        validationOptions = static_cast<MSIX_VALIDATION_OPTION>(validationOptions | MSIX_VALIDATION_OPTION::MSIX_VALIDATION_OPTION_SKIPSIGNATURE);
        return true;
    }

    bool AllowSignatureOriginUnknown()
    {
        validationOptions = static_cast<MSIX_VALIDATION_OPTION>(validationOptions | MSIX_VALIDATION_OPTION::MSIX_VALIDATION_OPTION_ALLOWSIGNATUREORIGINUNKNOWN);
        return true;
    }

    bool SetPackageName(const std::string& name)
    {
        if (!packageName.empty() || name.empty()) { return false; }
        packageName = name;
        return true;
    }

    bool SetDirectoryName(const std::string& name)
    {
        if (!directoryName.empty() || name.empty()) { return false; }
        directoryName = name;
        return true;
    }

    bool Validate()
    {
        switch (specified)
        {
        case UserSpecified::Unpack:            
            if (packageName.empty() || directoryName.empty()) {
                return false;
            }            
        }
        return true;
    }

    std::string packageName;
    std::string certName;
    std::string directoryName;
    UserSpecified specified                  = UserSpecified::Nothing;
    MSIX_VALIDATION_OPTION validationOptions = MSIX_VALIDATION_OPTION::MSIX_VALIDATION_OPTION_FULL;
    MSIX_PACKUNPACK_OPTION unpackOptions     = MSIX_PACKUNPACK_OPTION::MSIX_PACKUNPACK_OPTION_NONE;
};

// describes an option to a command that the user may specify
struct Option
{
    typedef bool (*CBF)(State& state, const std::string& value);

    Option(const std::string& name, bool param, const std::string& help, CBF callback) : 
        Name(name), Help(help), Callback(callback), TakesParameter(param)
    {}

    bool operator==(const std::string& rhs) {
        return Name == rhs;
    }

    bool        TakesParameter;
    std::string Name;
    std::string Help;
    CBF         Callback;
};

// describes a command that the user may specify.
struct Command
{
    typedef bool (*CBF)(State& state);

    Command(const std::string& name, const std::string& help, CBF callback, std::vector<Option> options) :
        Name(name), Help(help), Callback(callback), Options(options)
    {}

    bool operator==(const std::string& rhs) {
        return Name == rhs;
    }

    std::string         Name;
    std::string         Help;
    std::vector<Option> Options;
    CBF                 Callback;
};

// Displays contextual formatted help to the user.
int Help(char* toolName, std::vector<Command>& commands, State& state)
{
    std::cout << std::endl;
    std::cout << "Usage:" << std::endl;
    std::cout << "------" << std::endl;

    std::vector<Command>::iterator command;
    switch (state.specified)
    {
    case UserSpecified::Nothing:
    case UserSpecified::Help:
        std::cout << "    " << toolName << " <command> [options] " << std::endl;
        std::cout << std::endl;
        std::cout << "Valid commands:" << std::endl;
        std::cout << "---------------" << std::endl;
        for (const auto& c : commands)
        {
            std::cout << "    " << std::left << std::setfill(' ') << std::setw(10) <<
                c.Name << "--  " << c.Help << std::endl;
        }

        std::cout << std::endl;
        std::cout << "For help with a specific command, enter " << toolName << " <command> -?" << std::endl;
        return 0;
    case UserSpecified::Unpack:
        command = std::find(commands.begin(), commands.end(), "unpack");
        std::cout << "    " << toolName << " unpack -p <package> -d <directory> [options] " << std::endl;
        std::cout << std::endl;
        std::cout << "Description:" << std::endl;
        std::cout << "------------" << std::endl;
        std::cout << "    Extracts all files within an app package at the input <package> name to the" << std::endl;
        std::cout << "    specified output <directory>.  The output has the same directory structure " << std::endl;
        std::cout << "    as the package." << std::endl;
        break;
    }
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "--------" << std::endl;

    for (const auto& option : command->Options) {
        std::cout << "    " << std::left << std::setfill(' ') << std::setw(5) <<
            option.Name << ": " << option.Help << std::endl;
    }
    return 0;
}

// error text if the user provided underspecified input
void Error(char* toolName)
{
    std::cout << toolName << ": error : Missing required options.  Use '-?' for more details." << std::endl;
}

bool ParseInput(std::vector<Command>& commands, State& state, int argc, char* argv[])
{
    int index = 1;
    std::vector<Command>::iterator command;
    std::vector<Option>::iterator option;
    while (index < argc)
    {
        command = std::find(commands.begin(), commands.end(), argv[index]);
        if (command == commands.end()) {
            return false;
        }
        if (!command->Callback(state)) {
            return false;
        }
        if (++index == argc) {
            break;
        }
        option = std::find(command->Options.begin(), command->Options.end(), argv[index]);
        while (option != command->Options.end())
        {
            char const *parameter = "";
            if (option->TakesParameter) {
                if (++index == argc) {
                    break;
                }
                parameter = argv[index];
            }            
            if (!option->Callback(state, parameter)) {
                return false;
            }
            if (++index == argc) {
                break;
            }
            option = std::find(command->Options.begin(), command->Options.end(), argv[index]);
        }
    }
    return state.Validate();
}

// Parses argc/argv input via commands into state, and calls into the 
// appropriate function with the correct parameters if warranted.
int ParseAndRun(std::vector<Command>& commands, int argc, char* argv[])
{
    State state;
    if (!ParseInput(commands, state, argc, argv)) { 
        return Help(argv[0], commands, state);
    }

    switch (state.specified)
    {
    case UserSpecified::Help:
    case UserSpecified::Nothing:
        return Help(argv[0], commands, state);
    case UserSpecified::Unpack:
        return UnpackPackage(state.unpackOptions, state.validationOptions,
            const_cast<char*>(state.packageName.c_str()),
            const_cast<char*>(state.directoryName.c_str())
        );
    }
    return -1; // should never end up here.
}

LPVOID STDMETHODCALLTYPE MyAllocate(SIZE_T cb)  { return std::malloc(cb); }

class Text
{
public:
    char** operator&() { return &content; }
    ~Text() { Cleanup(); }

    char* content = nullptr;
    protected:    
    void Cleanup() { if (content) { std::free(content); content = nullptr; } }
};

// Defines the grammar of commands and each command's associated options,
int main(int argc, char* argv[])
{
    std::cout << "Microsoft (R) " << argv[0] << " version " << std::endl; // TODO: specify version
    std::cout << "Copyright (C) 2017 Microsoft.  All rights reserved." << std::endl;

    std::vector<Command> commands = {
        {   Command("unpack", "Unpack files from a package to disk",
                [](State& state) { return state.Specify(UserSpecified::Unpack); },
            {   
                Option("-p", true, "REQUIRED, specify input package name.",
                    [](State& state, const std::string& name) { return state.SetPackageName(name); }),
                Option("-d", true, "REQUIRED, specify output directory name.",
                    [](State& state, const std::string& name) { return state.SetDirectoryName(name); }),
                Option("-mv", false, "Skips manifest validation.  By default manifest validation is enabled.",
                    [](State& state, const std::string&) { return state.SkipManifestValidation(); }),
                Option("-sv", false, "Skips signature validation.  By default signature validation is enabled.",
                    [](State& state, const std::string&) { return state.AllowSignatureOriginUnknown(); }),
                Option("-ss", false, "Skips enforcement of signed packages.  By default packages must be signed.",
                    [](State& state, const std::string&) { return state.SkipSignature(); }),
                Option("-?", false, "Displays this help text.",
                    [](State& state, const std::string&) { return false; })                
            })
        },
        {   Command("-?", "Displays this help text.",
                [](State& state) { return state.Specify(UserSpecified::Help);}, {})
        },
    };

    auto result = ParseAndRun(commands, argc, argv);
    if (result != 0)
    {        
        std::cout << "Error: " << std::hex << result << std::endl;
        Text text;
        auto logResult = GetLogTextUTF8(MyAllocate, &text);
        if (0 == logResult)
        {
            std::cout << "LOG:" << std::endl << text.content << std::endl;
        }
        else 
        {
            std::cout << "UNABLE TO GET LOG WITH HR=" << std::hex << logResult << std::endl;
        }
    }
    return result;
}