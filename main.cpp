/*
 * MIT License
 *
 * Copyright (C) 2023 by wangwenx190 (Yuhang Zhao)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <syscmdline/system.h>
#include <syscmdline/option.h>
#include <syscmdline/command.h>
#include <syscmdline/parser.h>
#include <clang-c/Index.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <clocale>
#ifdef WIN32
#  include <windows.h>
#endif

namespace std
{
using stringlist = vector<string>;

[[nodiscard]] static inline std::string to_string(const CXCallingConv cc)
{
    switch (cc) {
    case CXCallingConv_Default:
    case CXCallingConv_C:
        return "__cdecl";
    case CXCallingConv_X86StdCall:
        return "__stdcall";
    case CXCallingConv_X86FastCall:
        return "__fastcall";
    case CXCallingConv_X86ThisCall:
        return "__thiscall";
    case CXCallingConv_X86Pascal:
        return "";
    case CXCallingConv_X86RegCall:
        return "";
    case CXCallingConv_X86VectorCall:
        return "__vectorcall";
    default:
        break;
    }
    return {};
}
}

namespace DWG
{

struct Function
{
    std::string name = {};
    std::string resultType = {};
    std::stringlist parameters = {};
    std::string callingConvention = {};
    
    [[nodiscard]] inline bool empty() const {
        return name.empty();
    }
    
    inline void clear() {
        name.clear();
        name.shrink_to_fit();
        resultType.clear();
        resultType.shrink_to_fit();
        parameters.clear();
        parameters.shrink_to_fit();
        callingConvention.clear();
        callingConvention.shrink_to_fit();
    }
};
using Functions = std::vector<Function>;

struct Header
{
    std::string filename = {};
    Functions functions = {};
    
    [[nodiscard]] inline bool empty() const {
        return filename.empty();
    }
    
    inline void clear() {
        filename.clear();
        filename.shrink_to_fit();
        functions.clear();
        functions.shrink_to_fit();
    }
};
using Headers = std::vector<Header>;

[[nodiscard]] static inline std::string toLower(const std::string_view str)
{
    if (str.empty()) {
        return {};
    }
    std::string result(str);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) -> char { return char(std::tolower(c)); });
    return result;
}

[[nodiscard]] static inline std::string toUpper(const std::string_view str)
{
    if (str.empty()) {
        return {};
    }
    std::string result(str);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) -> char { return char(std::toupper(c)); });
    return result;
}

[[nodiscard]] static inline constexpr bool isPointerType(const std::string_view type)
{
    return type.ends_with('*');
}

[[nodiscard]] static inline constexpr bool isReferenceType(const std::string_view type)
{
    return type.ends_with('&');
}

[[nodiscard]] static inline bool parseTranslationUnit(const std::string_view path, Functions &functionsOut)
{
    const CXIndex index = ::clang_createIndex(0, 0);
    const CXTranslationUnit unit = ::clang_parseTranslationUnit(index, path.data(), nullptr, 0, nullptr, 0, CXTranslationUnit_CacheCompletionResults | CXTranslationUnit_SkipFunctionBodies);
    
    if (!unit) {
        std::cerr << "libclang failed to parse the translation unit:" << path << std::endl;
        return false;
    }
    
    static Functions functions = {};
    static Function function = {};
    static bool insideParameterList = false;
    
    // They are static variables after all, we need to ensure they are all default value.
    functions.clear();
    function.clear();
    insideParameterList = false;
    
    const CXCursor cursor = ::clang_getTranslationUnitCursor(unit);
    const uint32_t parseResult = ::clang_visitChildren(cursor,
        [](CXCursor currentCursor, CXCursor parentCursor, CXClientData clientData) -> CXChildVisitResult {
            static_cast<void>(parentCursor);
            static_cast<void>(clientData);
            switch (::clang_getCursorKind(currentCursor)) {
            case CXCursor_FunctionDecl: {
                const CXLinkageKind linkage = ::clang_getCursorLinkage(currentCursor);
                if (linkage != CXLinkage_External) {
                    return CXChildVisit_Continue;
                }
                const CXVisibilityKind visibility = ::clang_getCursorVisibility(currentCursor);
                if (visibility != CXVisibility_Default) {
                    return CXChildVisit_Continue;
                }
                //const CXAvailabilityKind availability = ::clang_getCursorAvailability(currentCursor);
                const CXLanguageKind language = ::clang_getCursorLanguage(currentCursor);
                if (language != CXLanguage_C) {
                    return CXChildVisit_Continue;
                }
                if (insideParameterList) {
                    insideParameterList = false;
                    functions.push_back(function);
                    function = {};
                }
                const CXType functionType = ::clang_getCursorType(currentCursor);
                const CXString functionNameStr = ::clang_getCursorSpelling(currentCursor);
                function.name = ::clang_getCString(functionNameStr);
                ::clang_disposeString(functionNameStr);
                const CXType resultType = ::clang_getCursorResultType(currentCursor);
                const CXString resultStr = ::clang_getTypeSpelling(resultType);
                function.resultType = ::clang_getCString(resultStr);
                ::clang_disposeString(resultStr);
                const CXCallingConv callingConvention = ::clang_getFunctionTypeCallingConv(functionType);
                function.callingConvention = std::to_string(callingConvention);
//                ::CXString prettyStr = ::clang_getCursorPrettyPrinted(currentCursor, nullptr);
//                std::cout << "Pretty: " << ::clang_getCString(prettyStr) << std::endl;
//                ::clang_disposeString(prettyStr);
                return CXChildVisit_Recurse;
            }
            case CXCursor_ParmDecl: {
                if (!insideParameterList) {
                    insideParameterList = true;
                }
                const CXType parameterType = ::clang_getCursorType(currentCursor);
                const CXString parameterStr = ::clang_getTypeSpelling(parameterType);
                function.parameters.push_back(::clang_getCString(parameterStr));
                ::clang_disposeString(parameterStr);
                return CXChildVisit_Recurse;
            }
            default:
                return CXChildVisit_Continue;
            }
        }, nullptr);
    if (parseResult != 0) {
        std::cerr << "The parsing process was terminated prematurely." << std::endl;
        return false;
    }
    
    functionsOut = functions;
    return true;
}

[[nodiscard]] static inline bool generateWrapper(const std::string_view filePath, const std::string_view dllFileName, const Functions &functions)
{
    if (filePath.empty() || dllFileName.empty() || functions.empty()) {
        std::cerr << "generateWrapper: invalid parameter" << std::endl;
        return false;
    }
    std::ofstream out(std::string(filePath), std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
        std::cerr << "Failed to open file to write:" << filePath << std::endl;
        return false;
    }
    out << "#include <windows.h>" << std::endl;
    out << std::endl;
    out << "static constexpr const wchar_t kDllFileName[] = \"" << dllFileName << "\";" << std::endl;
    out << "static constinit HMODULE g_library = nullptr;" << std::endl;
    out << "static constinit bool g_libraryNotAvailable = false;" << std::endl;
    out << std::endl;
    for (auto &&function : std::as_const(functions)) {
        out << "extern \"C\" " << function.resultType;
        if (!(isPointerType(function.resultType) || isReferenceType(function.resultType))) {
            out << ' ';
        }
        out << "__stdcall " << function.name << '(';
        std::size_t parameterIndex = 1;
        for (auto &&parameter : std::as_const(function.parameters)) {
            out << parameter;
            if (!(isPointerType(parameter) || isReferenceType(parameter))) {
                out << ' ';
            }
            out << "arg" << parameterIndex;
            if (parameterIndex < function.parameters.size()) {
                ++parameterIndex;
                out << ", ";
            }
        }
        const std::string prototypeName = "PFN_" + toUpper(function.name);
        const std::string pointerName = "pfn_" + toLower(function.name);
        out << ')' << std::endl;
        out << '{' << std::endl;
        out << "    using " << prototypeName << " = decltype(&::" << function.name << ");" << std::endl;
        out << "    static const auto " << pointerName << " = []() -> " << prototypeName << " {" << std::endl;
        out << "        if (g_libraryNotAvailable) {" << std::endl;
        out << "            return nullptr;" << std::endl;
        out << "        }" << std::endl;
        out << "        if (!g_library) {" << std::endl;
        out << "            g_library = ::LoadLibraryExW(kDllFileName, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);" << std::endl;
        out << "            if (!g_library) {" << std::endl;
        out << "                g_libraryNotAvailable = true;" << std::endl;
        out << "                return nullptr;" << std::endl;
        out << "            }" << std::endl;
        out << "        }" << std::endl;
        out << "        return reinterpret_cast<" << prototypeName << ">(::GetProcAddress(g_library, \"" << function.name << "\"));" << std::endl;
        out << "    }();" << std::endl;
        out << "    if (!" << pointerName << ") {" << std::endl;
        if (function.resultType.empty() || function.resultType == "void") {
            out << "        return;" << std::endl;
        } else {
            out << "        return " << function.resultType << "{};" << std::endl;
        }
        out << "    }" << std::endl;
        out << "    return " << pointerName << '(';
        for (std::size_t index = 0; index != function.parameters.size(); ++index) {
            out << "arg" << index + 1;
            if (index < function.parameters.size() - 1) {
                out << ", ";
            }
        }
        out << ");" << std::endl;
        out << '}' << std::endl;
        out << std::endl;
    }
    //out.flush();
    out.close();
    std::cout << "The wrapper source is successfully generated." << std::endl;
    return true;
}

} // namespace DWG

extern "C" int __stdcall main(int, char **)
{
    std::setlocale(LC_ALL, "en_US.UTF-8");
#ifdef WIN32
    ::SetConsoleCP(CP_UTF8);
    ::SetConsoleOutputCP(CP_UTF8);
#endif
    SysCmdLine::Argument inputArgument("input-files");
    inputArgument.setDisplayName("<header files>");
    SysCmdLine::Option inputOption("input", "Header files to parse.");
    inputOption.addArgument(inputArgument);
    SysCmdLine::Argument outputArgument("output-file");
    outputArgument.setDisplayName("<source file>");
    SysCmdLine::Option outputOption("output", "The wrapper source file to generate.");
    outputOption.addArgument(outputArgument);
    SysCmdLine::Argument dllFileNameArgument("dll-filename");
    dllFileNameArgument.setDisplayName("<DLL file name>");
    SysCmdLine::Option dllFileNameOption("dll", "The DLL file name to load.");
    dllFileNameOption.addArgument(dllFileNameArgument);
    SysCmdLine::Command rootCommand(SysCmdLine::appName(), "A convenient tool to generate a wrapper layer for DLLs.");
    rootCommand.addVersionOption("1.0.0.0");
    rootCommand.addHelpOption(true, true);
    rootCommand.addOption(inputOption);
    rootCommand.addOption(outputOption);
    rootCommand.addOption(dllFileNameOption);
    rootCommand.setHandler([&](const SysCmdLine::ParseResult &result) -> int {
        const auto inputFiles = [&]() -> std::vector<SysCmdLine::Value> {
            const int optionCount = result.optionCount(inputOption);
            if (optionCount <= 0) {
                return {};
            }
            std::vector<SysCmdLine::Value> values = {};
            for (int index = 0; index != optionCount; ++index) {
                values.push_back(result.valueForOption(inputOption, inputArgument, index));
            }
            return values;
        }();
        const SysCmdLine::Value outputFile = result.valueForOption(outputOption, outputArgument);
        const SysCmdLine::Value dllFileName = result.valueForOption(dllFileNameOption, dllFileNameArgument);
        if (inputFiles.empty()) {
            std::cerr << "You need to specify at least one valid header file path (including the file extension name)." << std::endl;
            return EXIT_FAILURE;
        }
        if (outputFile.isEmpty()) {
            std::cerr << "You need to specify a valid output file path (including the file extension name)." << std::endl;
            return EXIT_FAILURE;
        }
        if (dllFileName.isEmpty()) {
            std::cerr << "You need to specify a valid DLL file name (better to include the file extension name as well)." << std::endl;
            return EXIT_FAILURE;
        }
        Functions allFunctions = {};
        for (auto &&inputFile : std::as_const(inputFiles)) {
            Functions functions = {};
            if (!parseTranslationUnit(inputFile.toString(), functions) || functions.empty()) {
                return EXIT_FAILURE;
            }
            allFunctions.insert(allFunctions.end(), functions.begin(), functions.end());
        }
        if (allFunctions.empty()) {
            return EXIT_FAILURE;
        }
        if (!generateWrapper(outputFile.toString(), dllFileName.toString(), allFunctions)) {
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    });
    SysCmdLine::Parser parser(rootCommand);
    parser.setDisplayOptions(SysCmdLine::Parser::ShowOptionalOptionsOnUsage);
    parser.setIntro(SysCmdLine::Parser::Prologue, "Thanks a lot for using DLL Wrapper Generator, a small tool from wangwenx190's utility tools collection.");
    parser.setIntro(SysCmdLine::Parser::Epilogue, "Please checkout https://github.com/wangwenx190/dll-wrapper-generator/ for more information.");
    return parser.invoke(SysCmdLine::commandLineArguments(), EXIT_FAILURE, SysCmdLine::Parser::IgnoreCommandCase | SysCmdLine::Parser::IgnoreOptionCase | SysCmdLine::Parser::AllowDosKeyValueOptions);
}
