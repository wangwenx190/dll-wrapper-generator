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
#include <ctime>
#include <iomanip>

namespace std
{
using stringlist = vector<string>;

[[nodiscard]] static inline std::string to_string(const CXCallingConv cc)
{
    switch (cc) {
    //case CXCallingConv_Default:
    case CXCallingConv_C:
        return "__cdecl";
    case CXCallingConv_X86StdCall:
        return "__stdcall";
    case CXCallingConv_X86FastCall:
        return "__fastcall";
    case CXCallingConv_X86ThisCall:
        return "__thiscall";
    case CXCallingConv_X86Pascal:
        return "__stdcall";
    case CXCallingConv_X86RegCall:
        return "__register";
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

[[nodiscard]] static inline std::string fromNativeSeparators(const std::string_view path)
{
    std::string result(path);
    std::replace(result.begin(), result.end(), '\\', '/');
    return result;
}

[[nodiscard]] static inline std::string toNativeSeparators(const std::string_view path)
{
    std::string result(path);
#ifdef WIN32
    std::replace(result.begin(), result.end(), '/', '\\');
#else
    std::replace(result.begin(), result.end(), '\\', '/');
#endif
    return result;
}

[[nodiscard]] static inline std::string extractFileName(const std::string_view path)
{
    std::string result = fromNativeSeparators(path);
    const std::size_t lastSeparatorIndex = result.find_last_of('/');
    if (lastSeparatorIndex != std::string::npos) {
        result.erase(0, lastSeparatorIndex + 1);
    }
    return result;
}

[[nodiscard]] static inline std::string extractDllFileBaseName(const std::string_view path)
{
    std::string fileName = extractFileName(path);
    if (fileName.starts_with("lib")) {
        fileName.erase(0, 3);
    }
    if (fileName.ends_with(".dll")) {
        fileName.erase(fileName.size() - 4, 4);
    }
    if (fileName.ends_with(".so")) {
        fileName.erase(fileName.size() - 3, 3);
    }
    if (fileName.ends_with(".dylib")) {
        fileName.erase(fileName.size() - 6, 6);
    }
    return fileName;
}

[[nodiscard]] static inline bool parseTranslationUnit(const std::string_view path, Functions &functionsOut)
{
    const CXIndex index = ::clang_createIndex(0, 0);
    std::uint32_t options = CXTranslationUnit_None;
    options |= CXTranslationUnit_Incomplete;
    options |= CXTranslationUnit_CacheCompletionResults;
    options |= CXTranslationUnit_SkipFunctionBodies;
    options |= CXTranslationUnit_KeepGoing;
    options |= CXTranslationUnit_SingleFileParse;
    options |= CXTranslationUnit_IgnoreNonErrorsFromIncludedFiles;
    options |= CXTranslationUnit_RetainExcludedConditionalBlocks;
    const CXTranslationUnit unit = ::clang_parseTranslationUnit(index, path.data(), nullptr, 0, nullptr, 0, options);

    if (!unit) {
        std::cerr << "libclang failed to parse the translation unit:" << path << std::endl;
        return false;
    }

    //static std::string fileName = {};
    static Functions functions = {};
    static Function function = {};
    static bool insideParameterList = false;

    // They are static variables after all, we need to ensure they are all expected initial value.
    //fileName = extractFileName(path);
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
                const CXString functionNameStr = ::clang_getCursorSpelling(currentCursor);
                const std::string functionName = ::clang_getCString(functionNameStr);
                ::clang_disposeString(functionNameStr);
                // Functions begin with "_" are usually internal stuff provided by the C runtime or toolchain,
                // they are not our target of course.
                if (functionName.starts_with('_')) {
                    return CXChildVisit_Continue;
                }
//                const CXSourceRange sourceRange = ::clang_getCursorExtent(currentCursor);
//                const CXSourceLocation sourceLocation = ::clang_getRangeStart(sourceRange);
//                CXFile sourceFile = nullptr;
//                ::clang_getExpansionLocation(sourceLocation, &sourceFile, nullptr, nullptr, nullptr);
//                const CXString sourceFileNameStr = ::clang_getFileName(sourceFile);
//                const std::string sourceFileName = extractFileName(::clang_getCString(sourceFileNameStr));
//                ::clang_disposeString(sourceFileNameStr);
//                if (sourceFileName != fileName) {
//                    return CXChildVisit_Continue;
//                }
                if (insideParameterList) {
                    insideParameterList = false;
                    functions.push_back(function);
                    function.clear();
                }
                function.name = functionName;
                const CXType functionType = ::clang_getCursorType(currentCursor);
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
                insideParameterList = true;
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

[[nodiscard]] static inline bool generateWrapper(const std::string_view filePath, const std::string_view dllFileName, const bool sysDirOnly, const Headers &headers)
{
    if (filePath.empty() || dllFileName.empty() || headers.empty()) {
        std::cerr << "generateWrapper: invalid parameter" << std::endl;
        return false;
    }
    std::ofstream out(std::string(filePath), std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
        std::cerr << "generateWrapper: failed to open file to write:" << filePath << std::endl;
        return false;
    }
    const std::time_t now = std::time(nullptr);
    out << "// GENERATED BY DLL WRAPPER GENERATOR ON " << std::put_time(std::localtime(&now), "%F %T %z") << std::endl;
    out << "#ifndef __EMSCRIPTEN__" << std::endl;
    out << "#ifdef WIN32" << std::endl;
    out << "#  include <windows.h>" << std::endl;
    out << "#  define DWG_API __stdcall" << std::endl;
    out << "#else" << std::endl;
    out << "#  include <dlfcn.h>" << std::endl;
    out << "#  define DWG_API" << std::endl;
    out << "#endif" << std::endl;
    out << "#include <string>" << std::endl;
    out << "using DWG_LibraryHandle = void *;" << std::endl;
    out << "using DWG_FunctionPointer = void(DWG_API *)();" << std::endl;
    out << "#ifdef WIN32" << std::endl;
    out << "[[nodiscard]] static inline DWG_LibraryHandle DWG_API DWG_LoadLibrary(const std::string_view path) { return ::LoadLibrary";
    if (sysDirOnly) {
        out << "ExA(path.data(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32";
    } else {
        out << "A(path.data()";
    }
    out << "); }" << std::endl;
    out << "[[nodiscard]] static inline DWG_FunctionPointer DWG_API DWG_GetProcAddress(const DWG_LibraryHandle library, const std::string_view name) { return reinterpret_cast<DWG_FunctionPointer>(::GetProcAddress(static_cast<HMODULE>(library), name.data())); }" << std::endl;
    out << "static inline void DWG_API DWG_FreeLibrary(const DWG_LibraryHandle library) { ::FreeLibrary(static_cast<HMODULE>(library)); }" << std::endl;
    out << "#else" << std::endl;
    out << "[[nodiscard]] static inline DWG_LibraryHandle DWG_API DWG_LoadLibrary(const std::string_view path) { return ::dlopen(path.data(), RTLD_LAZY); }" << std::endl;
    out << "[[nodiscard]] static inline DWG_FunctionPointer DWG_API DWG_GetProcAddress(const DWG_LibraryHandle library, const std::string_view name) { reinterpret_cast<DWG_FunctionPointer>(::dlsym(library, name.data())); }" << std::endl;
    out << "static inline void DWG_API DWG_FreeLibrary(const DWG_LibraryHandle library) { ::dlclose(library); }" << std::endl;
    out << "#endif" << std::endl;
    out << "[[nodiscard]] static inline DWG_LibraryHandle DWG_API DWG_TryGetLibrary() {" << std::endl;
    out << "    static const auto library = ::DWG_LoadLibrary(" << std::endl;
    out << "#ifdef WIN32" << std::endl;
    out << "        \"" << dllFileName << ".dll\"" << std::endl;
    out << "#elif defined(__APPLE__)" << std::endl;
    out << "        \"lib" << dllFileName << ".dylib\"" << std::endl;
    out << "#else" << std::endl;
    out << "        \"lib" << dllFileName << ".so\"" << std::endl;
    out << "#endif" << std::endl;
    out << "        );" << std::endl;
    out << "    return library;" << std::endl;
    out << '}' << std::endl;
    out << "[[nodiscard]] static inline DWG_FunctionPointer DWG_API DWG_TryGetSymbol(const std::string_view name) { if (const auto library = ::DWG_TryGetLibrary()) { return ::DWG_GetProcAddress(library, name); } else { return nullptr; } }" << std::endl;
    std::size_t totalFunctionCount = 0;
    for (auto &&header : std::as_const(headers)) {
        out << "#include <" << header.filename << '>' << std::endl;
        totalFunctionCount += header.functions.size();
    }
    for (auto &&header : std::as_const(headers)) {
        for (auto &&function : std::as_const(header.functions)) {
            out << "extern \"C\" " << function.resultType;
            if (!(isPointerType(function.resultType) || isReferenceType(function.resultType))) {
                out << ' ';
            }
            out << function.callingConvention << ' ' << function.name << '(';
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
            out << ") {" << std::endl;
            out << "    static const auto function = reinterpret_cast<decltype(&::" << function.name << ")>(::DWG_TryGetSymbol(\"" << function.name << "\"));" << std::endl;
            std::string functionCallStr = "function(";
            for (std::size_t index = 0; index != function.parameters.size(); ++index) {
                functionCallStr += "arg" + std::to_string(index + 1);
                if (index < function.parameters.size() - 1) {
                    functionCallStr += ", ";
                }
            }
            functionCallStr += ')';
            out << "    if (function) { ";
            if (function.resultType.empty() || function.resultType == "void") {
                out << functionCallStr << "; }";
            } else {
                out << "return " << functionCallStr << "; } else { return " << function.resultType << "{}; }";
            }
            out << std::endl;
            out << '}' << std::endl;
        }
    }
    out << "#endif" << std::endl;
    out << "// WRAPPED FUNCTION COUNT: " << totalFunctionCount << std::endl;
    out.close();
    std::cout << "The wrapper source is successfully generated." << std::endl;
    return true;
}

} // namespace DWG

extern "C" int
#ifdef WIN32
__stdcall
#else
#endif
main(int, char **)
{
    std::setlocale(LC_ALL, "en_US.UTF-8");
    SysCmdLine::Argument inputArgument("input-files");
    inputArgument.setDisplayName("<header files>");
    inputArgument.setMultiValueEnabled(true);
    SysCmdLine::Option inputOption({ "--input", "/input" }, "Header files to parse.");
    inputOption.setRequired(true);
    //inputOption.setUnlimitedOccurrence();
    inputOption.addArgument(inputArgument);
    SysCmdLine::Argument outputArgument("output-file");
    outputArgument.setDisplayName("<source file>");
    SysCmdLine::Option outputOption({ "--output", "/output" }, "The wrapper source file to generate.");
    outputOption.setRequired(true);
    outputOption.addArgument(outputArgument);
    SysCmdLine::Argument dllFileNameArgument("dll-filename");
    dllFileNameArgument.setDisplayName("<DLL file name>");
    SysCmdLine::Option dllFileNameOption({ "--dll", "/dll" }, "The DLL file name to load.");
    dllFileNameOption.setRequired(true);
    dllFileNameOption.addArgument(dllFileNameArgument);
    const SysCmdLine::Option sysDirOnlyOption({ "--sys-dir-only", "/sys-dir-only" }, "Only load DLL from the system directory.");
    SysCmdLine::Command rootCommand(SysCmdLine::appName(), "A convenient tool to generate a wrapper layer for DLLs.");
    rootCommand.addVersionOption("1.0.0.0");
    rootCommand.addHelpOption(true, true);
    rootCommand.addOption(inputOption);
    rootCommand.addOption(outputOption);
    rootCommand.addOption(dllFileNameOption);
    rootCommand.addOption(sysDirOnlyOption);
    rootCommand.setHandler([&](const SysCmdLine::ParseResult &result) -> int {
        const std::vector<SysCmdLine::Value> inputFiles = result.valuesForOption(inputOption);
        const SysCmdLine::Value outputFile = result.valueForOption(outputOption);
        const SysCmdLine::Value dllFileName = result.valueForOption(dllFileNameOption);
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
        const bool sysDirOnly = result.optionIsSet(sysDirOnlyOption);
        DWG::Headers headers = {};
        for (auto &&inputFile : std::as_const(inputFiles)) {
            const std::string filePath = inputFile.toString();
            DWG::Functions functions = {};
            if (!DWG::parseTranslationUnit(filePath, functions) || functions.empty()) {
                return EXIT_FAILURE;
            }
            DWG::Header header = {};
            header.filename = DWG::extractFileName(filePath);
            header.functions = functions;
            headers.push_back(header);
        }
        if (headers.empty()) {
            return EXIT_FAILURE;
        }
        const std::string dllFileBaseName = DWG::extractDllFileBaseName(dllFileName.toString());
        if (!DWG::generateWrapper(outputFile.toString(), dllFileBaseName, sysDirOnly, headers)) {
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    });
    SYSCMDLINE_ASSERT_COMMAND(rootCommand);
    SysCmdLine::Parser parser(rootCommand);
    parser.setDisplayOptions(SysCmdLine::Parser::ShowOptionalOptionsOnUsage);
    parser.setPrologue("Thanks a lot for using DLL Wrapper Generator, a small tool from wangwenx190's utility tools collection.");
    parser.setEpilogue("Please checkout https://github.com/wangwenx190/dll-wrapper-generator/ for more information.");
    return parser.invoke(SysCmdLine::commandLineArguments(), EXIT_FAILURE, SysCmdLine::Parser::IgnoreCommandCase | SysCmdLine::Parser::IgnoreOptionCase | SysCmdLine::Parser::AllowDosShortOptions);
}
