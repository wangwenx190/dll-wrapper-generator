#include <clang-c/Index.h>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>

namespace std
{
using stringlist = vector<string>;
}

struct Function
{
    std::string name = {};
    std::string resultType = {};
    std::stringlist parameters = {};
};
using Functions = std::vector<Function>;

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

[[nodiscard]] static inline bool isPointerType(const std::string_view type)
{
    return type.ends_with('*');
}

[[nodiscard]] static inline bool isReferenceType(const std::string_view type)
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
    
    const CXCursor cursor = ::clang_getTranslationUnitCursor(unit);
    static bool insideParameterList = false;
    const uint32_t parseResult = ::clang_visitChildren(cursor,
        [](CXCursor currentCursor, CXCursor parentCursor, CXClientData clientData) -> CXChildVisitResult {
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
//                const CXAvailabilityKind availability = ::clang_getCursorAvailability(currentCursor);
//                const CXLanguageKind language = ::clang_getCursorLanguage(currentCursor);
                if (insideParameterList) {
                    insideParameterList = false;
                    functions.push_back(function);
                    function = {};
                }
                const CXString functionName = ::clang_getCursorSpelling(currentCursor);
                function.name = ::clang_getCString(functionName);
                ::clang_disposeString(functionName);
                const CXType resultType = ::clang_getCursorResultType(currentCursor);
                const CXString resultStr = ::clang_getTypeSpelling(resultType);
                function.resultType = ::clang_getCString(resultStr);
                ::clang_disposeString(resultStr);                
//                ::CXString pretty = ::clang_getCursorPrettyPrinted(currentCursor, nullptr);
//                std::cout << "Pretty: " << ::clang_getCString(pretty) << std::endl;
//                ::clang_disposeString(pretty);
                return CXChildVisit_Recurse;
            }
            case CXCursor_ParmDecl: {
                if (!insideParameterList) {
                    insideParameterList = true;
                }
                const CXType type = ::clang_getCursorType(currentCursor);
                const CXString str = ::clang_getTypeSpelling(type);
                function.parameters.push_back(::clang_getCString(str));
                ::clang_disposeString(str);
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

[[nodiscard]] static inline bool generateWrapper(const std::string_view dll, const Functions &functions)
{
    if (functions.empty()) {
        return false;
    }
    std::cout << "#include <windows.h>" << std::endl;
    std::cout << std::endl;
    std::cout << "static constexpr const wchar_t kDllFileName[] = \"" << dll << "\";" << std::endl;
    std::cout << "static constinit HMODULE g_library = nullptr;" << std::endl;
    std::cout << "static constinit bool g_libraryNotAvailable = false;" << std::endl;
    std::cout << std::endl;
    for (auto &&function : std::as_const(functions)) {
        std::cout << "extern \"C\" " << function.resultType;
        if (!(isPointerType(function.resultType) || isReferenceType(function.resultType))) {
            std::cout << ' ';
        }
        std::cout << "__stdcall " << function.name << '(';
        std::size_t parameterIndex = 1;
        for (auto &&parameter : std::as_const(function.parameters)) {
            std::cout << parameter;
            if (!(isPointerType(parameter) || isReferenceType(parameter))) {
                std::cout << ' ';
            }
            std::cout << "arg" << parameterIndex;
            if (parameterIndex < function.parameters.size()) {
                ++parameterIndex;
                std::cout << ", ";
            }
        }
        const std::string prototypeName = "PFN_" + toUpper(function.name);
        const std::string pointerName = "pfn_" + toLower(function.name);
        std::cout << ')' << std::endl;
        std::cout << '{' << std::endl;
        std::cout << "    using " << prototypeName << " = decltype(&::" << function.name << ");" << std::endl;
        std::cout << "    static const auto " << pointerName << " = []() -> " << prototypeName << " {" << std::endl;
        std::cout << "        if (g_libraryNotAvailable) {" << std::endl;
        std::cout << "            return nullptr;" << std::endl;
        std::cout << "        }" << std::endl;
        std::cout << "        if (!g_library) {" << std::endl;
        std::cout << "            g_library = ::LoadLibraryExW(kDllFileName, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);" << std::endl;
        std::cout << "            if (!g_library) {" << std::endl;
        std::cout << "                g_libraryNotAvailable = true;" << std::endl;
        std::cout << "                return nullptr;" << std::endl;
        std::cout << "            }" << std::endl;
        std::cout << "        }" << std::endl;
        std::cout << "        return reinterpret_cast<" << prototypeName << ">(::GetProcAddress(g_library, \"" << function.name << "\"));" << std::endl;
        std::cout << "    }();" << std::endl;
        std::cout << "    if (!" << pointerName << ") {" << std::endl;
        if (function.resultType.empty() || function.resultType == "void") {
            std::cout << "        return;" << std::endl;
        } else {
            std::cout << "        return " << function.resultType << "{};" << std::endl;
        }
        std::cout << "    }" << std::endl;
        std::cout << "    return " << pointerName << '(';
        for (std::size_t index = 0; index != function.parameters.size(); ++index) {
            std::cout << "arg" << index + 1;
            if (index < function.parameters.size() - 1) {
                std::cout << ", ";
            }
        }
        std::cout << ");" << std::endl;
        std::cout << '}' << std::endl;
        std::cout << std::endl;
    }
    return true;
}

extern "C" int __stdcall main(int, char **)
{
    Functions functions = {};
    if (!parseTranslationUnit("icu.h", functions) || functions.empty()) {
        return EXIT_FAILURE;
    }
    if (!generateWrapper("icu.dll", functions)) {
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
