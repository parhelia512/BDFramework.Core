#include "il2cpp-config.h"
#include "MetadataCache.h"

#include <map>
#include <limits>
#include "il2cpp-class-internals.h"
#include "il2cpp-tabledefs.h"
#include "il2cpp-runtime-stats.h"
#include "gc/GarbageCollector.h"
#include "metadata/ArrayMetadata.h"
#include "metadata/GenericMetadata.h"
#include "metadata/GenericMethod.h"
#include "metadata/Il2CppTypeCompare.h"
#include "metadata/Il2CppTypeHash.h"
#include "metadata/Il2CppTypeVector.h"
#include "metadata/Il2CppGenericContextCompare.h"
#include "metadata/Il2CppGenericContextHash.h"
#include "metadata/Il2CppGenericInstCompare.h"
#include "metadata/Il2CppGenericInstHash.h"
#include "metadata/Il2CppGenericMethodCompare.h"
#include "metadata/Il2CppGenericMethodHash.h"
#include "metadata/Il2CppSignatureCompare.h"
#include "metadata/Il2CppSignatureHash.h"
#include "os/Atomic.h"
#include "os/Mutex.h"
#include "utils/CallOnce.h"
#include "utils/Collections.h"
#include "utils/HashUtils.h"
#include "utils/Il2CppHashMap.h"
#include "utils/Il2CppHashSet.h"
#include "utils/Memory.h"
#include "utils/StringUtils.h"
#include "utils/PathUtils.h"
#include "vm/Assembly.h"
#include "vm/Class.h"
#include "vm/ClassInlines.h"
#include "vm/GenericClass.h"
#include "vm/MetadataAlloc.h"
#include "vm/MetadataLoader.h"
#include "vm/MetadataLock.h"
#include "vm/Method.h"
#include "vm/Object.h"
#include "vm/String.h"
#include "vm/Type.h"
#include "mono-runtime/il2cpp-mapping.h"
#include "vm-utils/NativeSymbol.h"
#include "vm-utils/VmStringUtils.h"

#include "hybridclr/metadata/Assembly.h"
#include "hybridclr/metadata/MetadataModule.h"

using namespace il2cpp::vm;

typedef std::map<Il2CppClass*, Il2CppClass*> PointerTypeMap;
typedef Il2CppHashMap<const char*, Il2CppClass*, il2cpp::utils::StringUtils::StringHasher<const char*>, il2cpp::utils::VmStringUtils::CaseSensitiveComparer> WindowsRuntimeTypeNameToClassMap;
typedef Il2CppHashMap<const Il2CppClass*, const char*, il2cpp::utils::PointerHash<Il2CppClass> > ClassToWindowsRuntimeTypeNameMap;

typedef Il2CppHashSet<const Il2CppGenericMethod*, il2cpp::metadata::Il2CppGenericMethodHash, il2cpp::metadata::Il2CppGenericMethodCompare> Il2CppGenericMethodSet;
typedef Il2CppGenericMethodSet::const_iterator Il2CppGenericMethodSetIter;
static Il2CppGenericMethodSet s_GenericMethodSet;

struct Il2CppMetadataCache
{
    il2cpp::os::FastMutex m_CacheMutex;
    PointerTypeMap m_PointerTypes;
};

static Il2CppMetadataCache s_MetadataCache;
static Il2CppClass** s_TypeInfoTable = NULL;
static Il2CppClass** s_TypeInfoDefinitionTable = NULL;
static const MethodInfo** s_MethodInfoDefinitionTable = NULL;
static Il2CppString** s_StringLiteralTable = NULL;
static const Il2CppGenericMethod** s_GenericMethodTable = NULL;
static int32_t s_ImagesCount = 0;
static Il2CppImage* s_ImagesTable = NULL;
static int32_t s_AssembliesCount = 0;
static Il2CppAssembly* s_AssembliesTable = NULL;


typedef Il2CppHashSet<const Il2CppGenericInst*, il2cpp::metadata::Il2CppGenericInstHash, il2cpp::metadata::Il2CppGenericInstCompare> Il2CppGenericInstSet;
static Il2CppGenericInstSet s_GenericInstSet;

typedef Il2CppHashMap<const Il2CppGenericMethod*, const Il2CppGenericMethodIndices*, il2cpp::metadata::Il2CppGenericMethodHash, il2cpp::metadata::Il2CppGenericMethodCompare> Il2CppMethodTableMap;
typedef Il2CppMethodTableMap::const_iterator Il2CppMethodTableMapIter;
static Il2CppMethodTableMap s_MethodTableMap;

typedef Il2CppHashMap<il2cpp::utils::dynamic_array<const Il2CppType*>, Il2CppMethodPointer, il2cpp::metadata::Il2CppSignatureHash, il2cpp::metadata::Il2CppSignatureCompare> Il2CppUnresolvedSignatureMap;
typedef Il2CppUnresolvedSignatureMap::const_iterator Il2CppUnresolvedSignatureMapIter;
static Il2CppUnresolvedSignatureMap *s_pUnresolvedSignatureMap;

typedef Il2CppHashMap<FieldInfo*, int32_t, il2cpp::utils::PointerHash<FieldInfo> > Il2CppThreadLocalStaticOffsetHashMap;
typedef Il2CppThreadLocalStaticOffsetHashMap::iterator Il2CppThreadLocalStaticOffsetHashMapIter;
static Il2CppThreadLocalStaticOffsetHashMap s_ThreadLocalStaticOffsetMap;

static const Il2CppCodeRegistration * s_Il2CppCodeRegistration;
static const Il2CppMetadataRegistration * s_Il2CppMetadataRegistration;
static const Il2CppCodeGenOptions* s_Il2CppCodeGenOptions;
static CustomAttributesCache** s_CustomAttributesCaches;

static WindowsRuntimeTypeNameToClassMap s_WindowsRuntimeTypeNameToClassMap;
static ClassToWindowsRuntimeTypeNameMap s_ClassToWindowsRuntimeTypeNameMap;

struct InteropDataToTypeConverter
{
    inline const Il2CppType* operator()(const Il2CppInteropData& interopData) const
    {
        return interopData.type;
    }
};

typedef il2cpp::utils::collections::ArrayValueMap<const Il2CppType*, Il2CppInteropData, InteropDataToTypeConverter, il2cpp::metadata::Il2CppTypeLess, il2cpp::metadata::Il2CppTypeEqualityComparer> InteropDataMap;
static InteropDataMap s_InteropData;

struct WindowsRuntimeFactoryTableEntryToTypeConverter
{
    inline const Il2CppType* operator()(const Il2CppWindowsRuntimeFactoryTableEntry& entry) const
    {
        return entry.type;
    }
};

typedef il2cpp::utils::collections::ArrayValueMap<const Il2CppType*, Il2CppWindowsRuntimeFactoryTableEntry, WindowsRuntimeFactoryTableEntryToTypeConverter, il2cpp::metadata::Il2CppTypeLess, il2cpp::metadata::Il2CppTypeEqualityComparer> WindowsRuntimeFactoryTable;
static WindowsRuntimeFactoryTable s_WindowsRuntimeFactories;

template<typename K, typename V>
struct PairToKeyConverter
{
    inline const K& operator()(const std::pair<K, V>& pair) const
    {
        return pair.first;
    }
};

typedef il2cpp::utils::collections::ArrayValueMap<const Il2CppGuid*, std::pair<const Il2CppGuid*, Il2CppClass*>, PairToKeyConverter<const Il2CppGuid*, Il2CppClass*> > GuidToClassMap;
static GuidToClassMap s_GuidToNonImportClassMap;

static il2cpp::utils::dynamic_array<Il2CppAssembly*> s_cliAssemblies;

template<typename T>
static T MetadataOffset(void* metadata, size_t sectionOffset, size_t itemIndex)
{
    return reinterpret_cast<T>(reinterpret_cast<uint8_t*>(metadata) + sectionOffset) + itemIndex;
}

void il2cpp::vm::MetadataCache::Register(const Il2CppCodeRegistration* const codeRegistration, const Il2CppMetadataRegistration* const metadataRegistration, const Il2CppCodeGenOptions* const codeGenOptions)
{
    s_Il2CppCodeRegistration = codeRegistration;
    s_Il2CppMetadataRegistration = metadataRegistration;
    s_Il2CppCodeGenOptions = codeGenOptions;

    s_InteropData.assign_external(codeRegistration->interopData, codeRegistration->interopDataCount);
    s_WindowsRuntimeFactories.assign_external(codeRegistration->windowsRuntimeFactoryTable, codeRegistration->windowsRuntimeFactoryCount);
}

static void* s_GlobalMetadata;
static const Il2CppGlobalMetadataHeader* s_GlobalMetadataHeader;

typedef void (*Il2CppTypeUpdater)(Il2CppType*);

static void InitializeTypeHandle(Il2CppType* type)
{
    type->data.typeHandle = il2cpp::vm::GlobalMetadata::GetTypeHandleFromIndex(type->data.__klassIndex);
}

static void ClearTypeHandle(Il2CppType* type)
{
    type->data.__klassIndex = -1; // GetIndexForTypeDefinitionInternal(reinterpret_cast<const Il2CppTypeDefinition*>(type->data.typeHandle));
}

static void InitializeGenericParameterHandle(Il2CppType* type)
{
    type->data.genericParameterHandle = il2cpp::vm::MetadataCache::GetGenericParameterFromIndex(type->data.__genericParameterIndex);
}

static void ClearGenericParameterHandle(Il2CppType* type)
{
    type->data.__genericParameterIndex = -1;// GetIndexForGenericParameter(reinterpret_cast<Il2CppMetadataGenericParameterHandle>(type->data.genericParameterHandle));
}

static void ProcessIl2CppTypeDefinitions(Il2CppTypeUpdater updateTypeDef, Il2CppTypeUpdater updateGenericParam)
{
    for (int32_t i = 0; i < s_Il2CppMetadataRegistration->typesCount; i++)
    {
        const Il2CppType* type = s_Il2CppMetadataRegistration->types[i];
        switch (type->type)
        {
        case IL2CPP_TYPE_VOID:
        case IL2CPP_TYPE_BOOLEAN:
        case IL2CPP_TYPE_CHAR:
        case IL2CPP_TYPE_I1:
        case IL2CPP_TYPE_U1:
        case IL2CPP_TYPE_I2:
        case IL2CPP_TYPE_U2:
        case IL2CPP_TYPE_I4:
        case IL2CPP_TYPE_U4:
        case IL2CPP_TYPE_I8:
        case IL2CPP_TYPE_U8:
        case IL2CPP_TYPE_R4:
        case IL2CPP_TYPE_R8:
        case IL2CPP_TYPE_STRING:
        case IL2CPP_TYPE_VALUETYPE:
        case IL2CPP_TYPE_CLASS:
        case IL2CPP_TYPE_I:
        case IL2CPP_TYPE_U:
        case IL2CPP_TYPE_OBJECT:
        case IL2CPP_TYPE_TYPEDBYREF:
            // The Il2Cpp conversion process writes these types in a writeable section
            // So we can const_cast them here safely
            updateTypeDef(const_cast<Il2CppType*>(type));
            break;
        case IL2CPP_TYPE_VAR:
        case IL2CPP_TYPE_MVAR:
            updateGenericParam(const_cast<Il2CppType*>(type));
            break;
        default:
            // Nothing do to
            break;
        }
    }
}

bool il2cpp::vm::MetadataCache::Initialize()
{
    s_GlobalMetadata = vm::MetadataLoader::LoadMetadataFile("global-metadata.dat");
    if (!s_GlobalMetadata)
        return false;

    il2cpp::metadata::GenericMetadata::SetMaximumRuntimeGenericDepth(s_Il2CppCodeGenOptions->maximumRuntimeGenericDepth);

    s_GlobalMetadataHeader = (const Il2CppGlobalMetadataHeader*)s_GlobalMetadata;
    IL2CPP_ASSERT(s_GlobalMetadataHeader->sanity == 0xFAB11BAF);
    IL2CPP_ASSERT(s_GlobalMetadataHeader->version == 24);

    // Pre-allocate these arrays so we don't need to lock when reading later.
    // These arrays hold the runtime metadata representation for metadata explicitly
    // referenced during conversion. There is a corresponding table of same size
    // in the converted metadata, giving a description of runtime metadata to construct.
    s_TypeInfoTable = (Il2CppClass**)IL2CPP_CALLOC(s_Il2CppMetadataRegistration->typesCount, sizeof(Il2CppClass*));
    s_TypeInfoDefinitionTable = (Il2CppClass**)IL2CPP_CALLOC(s_GlobalMetadataHeader->typeDefinitionsCount / sizeof(Il2CppTypeDefinition), sizeof(Il2CppClass*));
    s_MethodInfoDefinitionTable = (const MethodInfo**)IL2CPP_CALLOC(s_GlobalMetadataHeader->methodsCount / sizeof(Il2CppMethodDefinition), sizeof(MethodInfo*));
    s_GenericMethodTable = (const Il2CppGenericMethod**)IL2CPP_CALLOC(s_Il2CppMetadataRegistration->methodSpecsCount, sizeof(Il2CppGenericMethod*));
    s_ImagesCount = s_GlobalMetadataHeader->imagesCount / sizeof(Il2CppImageDefinition);
    s_ImagesTable = (Il2CppImage*)IL2CPP_CALLOC(s_ImagesCount, sizeof(Il2CppImage));
    s_AssembliesCount = s_GlobalMetadataHeader->assembliesCount / sizeof(Il2CppAssemblyDefinition);
    s_AssembliesTable = (Il2CppAssembly*)IL2CPP_CALLOC(s_AssembliesCount, sizeof(Il2CppAssembly));

    // setup all the Il2CppImages. There are not many and it avoid locks later on
    const Il2CppImageDefinition* imagesDefinitions = (const Il2CppImageDefinition*)((const char*)s_GlobalMetadata + s_GlobalMetadataHeader->imagesOffset);
    for (int32_t imageIndex = 0; imageIndex < s_ImagesCount; imageIndex++)
    {
        const Il2CppImageDefinition* imageDefinition = imagesDefinitions + imageIndex;
        Il2CppImage* image = s_ImagesTable + imageIndex;
        image->name = GetStringFromIndex(imageDefinition->nameIndex);

        std::string nameNoExt = il2cpp::utils::PathUtils::PathNoExtension(image->name);
        image->nameNoExt = (char*)IL2CPP_CALLOC(nameNoExt.size() + 1, sizeof(char));
        strcpy(const_cast<char*>(image->nameNoExt), nameNoExt.c_str());

        image->assembly = const_cast<Il2CppAssembly*>(GetAssemblyFromIndex(image, imageDefinition->assemblyIndex));
        image->typeStart = imageDefinition->typeStart;
        image->typeCount = imageDefinition->typeCount;
        image->exportedTypeStart = imageDefinition->exportedTypeStart;
        image->exportedTypeCount = imageDefinition->exportedTypeCount;
        image->entryPointIndex = imageDefinition->entryPointIndex;
        image->token = imageDefinition->token;
        image->customAttributeStart = imageDefinition->customAttributeStart;
        image->customAttributeCount = imageDefinition->customAttributeCount;
        for (uint32_t codeGenModuleIndex = 0; codeGenModuleIndex < s_Il2CppCodeRegistration->codeGenModulesCount; ++codeGenModuleIndex)
        {
            if (strcmp(image->name, s_Il2CppCodeRegistration->codeGenModules[codeGenModuleIndex]->moduleName) == 0)
                image->codeGenModule = s_Il2CppCodeRegistration->codeGenModules[codeGenModuleIndex];
        }
        IL2CPP_ASSERT(image->codeGenModule);
        image->dynamic = false;
    }

    // setup all the Il2CppAssemblies.
    const Il2CppAssemblyDefinition* assemblyDefinitions = (const Il2CppAssemblyDefinition*)((const char*)s_GlobalMetadata + s_GlobalMetadataHeader->assembliesOffset);
    for (int32_t assemblyIndex = 0; assemblyIndex < s_ImagesCount; assemblyIndex++)
    {
        const Il2CppAssemblyDefinition* assemblyDefinition = assemblyDefinitions + assemblyIndex;
        Il2CppAssembly* assembly = s_AssembliesTable + assemblyIndex;

        assembly->image = il2cpp::vm::MetadataCache::GetImageFromIndex(assemblyDefinition->imageIndex);
        assembly->token = assemblyDefinition->token;
        assembly->referencedAssemblyStart = assemblyDefinition->referencedAssemblyStart;
        assembly->referencedAssemblyCount = assemblyDefinition->referencedAssemblyCount;

        Il2CppAssemblyName* assemblyName = &assembly->aname;
        const Il2CppAssemblyNameDefinition* assemblyNameDefinition = &assemblyDefinition->aname;

        assemblyName->name = GetStringFromIndex(assemblyNameDefinition->nameIndex);
        assemblyName->culture = GetStringFromIndex(assemblyNameDefinition->cultureIndex);
        assemblyName->public_key = (const uint8_t*)GetStringFromIndex(assemblyNameDefinition->publicKeyIndex);
        assemblyName->hash_alg = assemblyNameDefinition->hash_alg;
        assemblyName->hash_len = assemblyNameDefinition->hash_len;
        assemblyName->flags = assemblyNameDefinition->flags;
        assemblyName->major = assemblyNameDefinition->major;
        assemblyName->minor = assemblyNameDefinition->minor;
        assemblyName->build = assemblyNameDefinition->build;
        assemblyName->revision = assemblyNameDefinition->revision;
        memcpy(assemblyName->public_key_token, assemblyNameDefinition->public_key_token, sizeof(assemblyNameDefinition->public_key_token));

        il2cpp::vm::Assembly::Register(assembly);
    }

    ProcessIl2CppTypeDefinitions(InitializeTypeHandle, InitializeGenericParameterHandle);

    for (int32_t j = 0; j < s_Il2CppMetadataRegistration->genericClassesCount; j++)
    {
        Il2CppGenericClass* genericClass = s_Il2CppMetadataRegistration->genericClasses[j];
        if (genericClass->__typeDefinitionIndex != kTypeIndexInvalid)
        {
            const Il2CppTypeDefinition* typeDefinition = GetTypeDefinitionFromIndex(genericClass->__typeDefinitionIndex);
            genericClass->type = il2cpp::vm::MetadataCache::GetIl2CppTypeFromIndex(typeDefinition->byvalTypeIndex);
        }
        else
        {
            genericClass->type = nullptr;
        }
    }

    for (int32_t j = 0; j < s_Il2CppMetadataRegistration->genericClassesCount; j++)
        if (s_Il2CppMetadataRegistration->genericClasses[j]->type)
            il2cpp::metadata::GenericMetadata::RegisterGenericClass(s_Il2CppMetadataRegistration->genericClasses[j]);

    for (int32_t i = 0; i < s_Il2CppMetadataRegistration->genericInstsCount; i++)
        s_GenericInstSet.insert(s_Il2CppMetadataRegistration->genericInsts[i]);

    InitializeUnresolvedSignatureTable();

#if IL2CPP_ENABLE_NATIVE_STACKTRACES
    std::vector<MethodDefinitionKey> managedMethods;


    const Il2CppTypeDefinition* typeDefinitions = (const Il2CppTypeDefinition*)((const char*)s_GlobalMetadata + s_GlobalMetadataHeader->typeDefinitionsOffset);
    for (int32_t i = 0; i < s_AssembliesCount; i++)
    {
        const Il2CppImage* image = s_AssembliesTable[i].image;

        for (size_t j = 0; j < image->typeCount; j++)
        {
            const Il2CppTypeDefinition* type = typeDefinitions + image->typeStart + j;

            for (uint16_t u = 0; u < type->method_count; u++)
            {
                const Il2CppMethodDefinition* methodDefinition = GetMethodDefinitionFromIndex(type->methodStart + u);
                MethodDefinitionKey currentMethodList;
                currentMethodList.methodIndex = type->methodStart + u;
                currentMethodList.method = GetMethodPointer(image, methodDefinition->token);
                if (currentMethodList.method)
                    managedMethods.push_back(currentMethodList);
            }
        }
    }

    for (int32_t i = 0; i < s_Il2CppMetadataRegistration->genericMethodTableCount; i++)
    {
        const Il2CppGenericMethodFunctionsDefinitions* genericMethodIndices = s_Il2CppMetadataRegistration->genericMethodTable + i;

        MethodDefinitionKey currentMethodList;

        GenericMethodIndex genericMethodIndex = genericMethodIndices->genericMethodIndex;

        IL2CPP_ASSERT(genericMethodIndex < s_Il2CppMetadataRegistration->methodSpecsCount);
        const Il2CppMethodSpec* methodSpec = s_Il2CppMetadataRegistration->methodSpecs + genericMethodIndex;

        currentMethodList.methodIndex = methodSpec->methodDefinitionIndex;

        IL2CPP_ASSERT(genericMethodIndices->indices.methodIndex < static_cast<int32_t>(s_Il2CppCodeRegistration->genericMethodPointersCount));
        currentMethodList.method = s_Il2CppCodeRegistration->genericMethodPointers[genericMethodIndices->indices.methodIndex];

        managedMethods.push_back(currentMethodList);
    }

    il2cpp::utils::NativeSymbol::RegisterMethods(managedMethods);
#endif

    return true;
}

void il2cpp::vm::MetadataCache::InitializeStringLiteralTable()
{
    s_StringLiteralTable = (Il2CppString**)il2cpp::gc::GarbageCollector::AllocateFixed(s_GlobalMetadataHeader->stringLiteralCount / sizeof(Il2CppStringLiteral) * sizeof(Il2CppString*), NULL);
}

void il2cpp::vm::MetadataCache::InitializeGenericMethodTable()
{
    for (int32_t i = 0; i < s_Il2CppMetadataRegistration->genericMethodTableCount; i++)
    {
        const Il2CppGenericMethodFunctionsDefinitions* genericMethodIndices = s_Il2CppMetadataRegistration->genericMethodTable + i;
        const Il2CppGenericMethod* genericMethod = GetGenericMethodFromIndex(genericMethodIndices->genericMethodIndex);
        s_MethodTableMap.insert(std::make_pair(genericMethod, &genericMethodIndices->indices));
    }
}

void il2cpp::vm::MetadataCache::InitializeWindowsRuntimeTypeNamesTables()
{
    int32_t typeCount = s_GlobalMetadataHeader->windowsRuntimeTypeNamesSize / sizeof(Il2CppWindowsRuntimeTypeNamePair);
    const Il2CppWindowsRuntimeTypeNamePair* windowsRuntimeTypeNames = MetadataOffset<Il2CppWindowsRuntimeTypeNamePair*>(s_GlobalMetadata, s_GlobalMetadataHeader->windowsRuntimeTypeNamesOffset, 0);

    for (int32_t i = 0; i < typeCount; i++)
    {
        Il2CppWindowsRuntimeTypeNamePair typeNamePair = windowsRuntimeTypeNames[i];
        const char* name = GetStringFromIndex(typeNamePair.nameIndex);
        const Il2CppType* type = GetIl2CppTypeFromIndex(typeNamePair.typeIndex);
        Il2CppClass* klass = il2cpp::vm::Class::FromIl2CppType(type);

        if (!Class::IsNullable(klass))
        {
            // Don't add nullable types to name -> klass map because IReference`1<T> and Nullable`1<T>
            // share windows runtime type names, and that would cause a collision.
            s_WindowsRuntimeTypeNameToClassMap.insert(std::make_pair(name, klass));
        }

        s_ClassToWindowsRuntimeTypeNameMap.insert(std::make_pair(klass, name));
    }
}

void il2cpp::vm::MetadataCache::InitializeGuidToClassTable()
{
    Il2CppInteropData* interopData = s_Il2CppCodeRegistration->interopData;
    uint32_t interopDataCount = s_Il2CppCodeRegistration->interopDataCount;
    std::vector<std::pair<const Il2CppGuid*, Il2CppClass*> > guidToNonImportClassMap;
    guidToNonImportClassMap.reserve(interopDataCount);

    for (uint32_t i = 0; i < interopDataCount; i++)
    {
        // It's important to check for non-import types because type projections will have identical GUIDs (e.g. IEnumerable<T> and IIterable<T>)
        if (interopData[i].guid != NULL)
        {
            Il2CppClass* klass = Class::FromIl2CppType(interopData[i].type);
            if (!klass->is_import_or_windows_runtime)
                guidToNonImportClassMap.push_back(std::make_pair(interopData[i].guid, klass));
        }
    }

    s_GuidToNonImportClassMap.assign(guidToNonImportClassMap);
}

// this is called later in the intialization cycle with more systems setup like GC
void il2cpp::vm::MetadataCache::InitializeGCSafe()
{
    InitializeStringLiteralTable();
    InitializeGenericMethodTable();
    InitializeWindowsRuntimeTypeNamesTables();
    InitializeGuidToClassTable();
}

void il2cpp::vm::MetadataCache::InitializeUnresolvedSignatureTable()
{
    s_pUnresolvedSignatureMap = new Il2CppUnresolvedSignatureMap();

    for (uint32_t i = 0; i < s_Il2CppCodeRegistration->unresolvedVirtualCallCount; ++i)
    {
        const Il2CppRange* range = MetadataOffset<Il2CppRange*>(s_GlobalMetadata, s_GlobalMetadataHeader->unresolvedVirtualCallParameterRangesOffset, i);
        il2cpp::utils::dynamic_array<const Il2CppType*> signature;

        for (int j = 0; j < range->length; ++j)
        {
            TypeIndex typeIndex = *MetadataOffset<TypeIndex*>(s_GlobalMetadata, s_GlobalMetadataHeader->unresolvedVirtualCallParameterTypesOffset, range->start + j);
            const Il2CppType* type = il2cpp::vm::MetadataCache::GetIl2CppTypeFromIndex(typeIndex);
            signature.push_back(type);
        }

        (*s_pUnresolvedSignatureMap)[signature] = s_Il2CppCodeRegistration->unresolvedVirtualCallPointers[i];
    }
}

Il2CppClass* il2cpp::vm::MetadataCache::GetGenericInstanceType(Il2CppClass* genericTypeDefinition, const il2cpp::metadata::Il2CppTypeVector& genericArgumentTypes)
{
    const Il2CppGenericInst* inst = il2cpp::vm::MetadataCache::GetGenericInst(genericArgumentTypes);
    Il2CppGenericClass* genericClass = il2cpp::metadata::GenericMetadata::GetGenericClass(genericTypeDefinition, inst);
    return il2cpp::vm::GenericClass::GetClass(genericClass);
}

const MethodInfo* il2cpp::vm::MetadataCache::GetGenericInstanceMethod(const MethodInfo* genericMethodDefinition, const Il2CppGenericContext* context)
{
    const MethodInfo* method = genericMethodDefinition;
    const Il2CppGenericInst* classInst = context->class_inst;
    const Il2CppGenericInst* methodInst = context->method_inst;
    if (genericMethodDefinition->is_inflated)
    {
        IL2CPP_ASSERT(genericMethodDefinition->klass->generic_class);
        classInst = genericMethodDefinition->klass->generic_class->context.class_inst;
        method = genericMethodDefinition->genericMethod->methodDefinition;
    }

    const Il2CppGenericMethod* gmethod = GetGenericMethod(method, classInst, methodInst);
    return il2cpp::metadata::GenericMethod::GetMethod(gmethod);
}

const MethodInfo* il2cpp::vm::MetadataCache::GetGenericInstanceMethod(const MethodInfo* genericMethodDefinition, const il2cpp::metadata::Il2CppTypeVector& genericArgumentTypes)
{
    Il2CppGenericContext context = { NULL, GetGenericInst(genericArgumentTypes) };

    return GetGenericInstanceMethod(genericMethodDefinition, &context);
}

const Il2CppGenericContext* il2cpp::vm::MetadataCache::GetMethodGenericContext(const MethodInfo* method)
{
    if (!method->is_inflated)
    {
        IL2CPP_NOT_IMPLEMENTED(Image::GetMethodGenericContext);
        return NULL;
    }

    return &method->genericMethod->context;
}

const MethodInfo* il2cpp::vm::MetadataCache::GetGenericMethodDefinition(const MethodInfo* method)
{
    if (!method->is_inflated)
    {
        IL2CPP_NOT_IMPLEMENTED(Image::GetGenericMethodDefinition);
        return NULL;
    }

    return method->genericMethod->methodDefinition;
}

const Il2CppGenericContainer* il2cpp::vm::MetadataCache::GetMethodGenericContainer(const MethodInfo* method)
{
    return method->genericContainer;
}

Il2CppClass* il2cpp::vm::MetadataCache::GetPointerType(Il2CppClass* type)
{
    il2cpp::os::FastAutoLock lock(&s_MetadataCache.m_CacheMutex);

    PointerTypeMap::const_iterator i = s_MetadataCache.m_PointerTypes.find(type);
    if (i == s_MetadataCache.m_PointerTypes.end())
        return NULL;

    return i->second;
}

Il2CppClass* il2cpp::vm::MetadataCache::GetWindowsRuntimeClass(const char* fullName)
{
    WindowsRuntimeTypeNameToClassMap::iterator it = s_WindowsRuntimeTypeNameToClassMap.find(fullName);
    if (it != s_WindowsRuntimeTypeNameToClassMap.end())
        return it->second;

    return NULL;
}

const char* il2cpp::vm::MetadataCache::GetWindowsRuntimeClassName(const Il2CppClass* klass)
{
    ClassToWindowsRuntimeTypeNameMap::iterator it = s_ClassToWindowsRuntimeTypeNameMap.find(klass);
    if (it != s_ClassToWindowsRuntimeTypeNameMap.end())
        return it->second;

    return NULL;
}

Il2CppMethodPointer il2cpp::vm::MetadataCache::GetWindowsRuntimeFactoryCreationFunction(const char* fullName)
{
    Il2CppClass* klass = GetWindowsRuntimeClass(fullName);
    if (klass == NULL)
        return NULL;

    WindowsRuntimeFactoryTable::iterator factoryEntry = s_WindowsRuntimeFactories.find_first(&klass->byval_arg);
    if (factoryEntry == s_WindowsRuntimeFactories.end())
        return NULL;

    return factoryEntry->createFactoryFunction;
}

Il2CppClass* il2cpp::vm::MetadataCache::GetClassForGuid(const Il2CppGuid* guid)
{
    IL2CPP_ASSERT(guid != NULL);

    GuidToClassMap::iterator it = s_GuidToNonImportClassMap.find_first(guid);
    if (it != s_GuidToNonImportClassMap.end())
        return it->second;

    return NULL;
}

void il2cpp::vm::MetadataCache::AddPointerType(Il2CppClass* type, Il2CppClass* pointerType)
{
    il2cpp::os::FastAutoLock lock(&s_MetadataCache.m_CacheMutex);
    s_MetadataCache.m_PointerTypes.insert(std::make_pair(type, pointerType));
}

const Il2CppGenericInst* il2cpp::vm::MetadataCache::GetGenericInst(const Il2CppType* const* types, uint32_t typeCount)
{
    // temporary inst to lookup a permanent one that may already exist
    Il2CppGenericInst inst;
    inst.type_argc = typeCount;
    inst.type_argv = (const Il2CppType**)alloca(inst.type_argc * sizeof(Il2CppType*));

    size_t index = 0;
    const Il2CppType* const* typesEnd = types + typeCount;
    for (const Il2CppType* const* iter = types; iter != typesEnd; ++iter, ++index)
        inst.type_argv[index] = *iter;

    {
        // Acquire lock to check if inst has already been cached.
        il2cpp::os::FastAutoLock lock(&s_MetadataCache.m_CacheMutex);
        Il2CppGenericInstSet::const_iterator iter = s_GenericInstSet.find(&inst);
        if (iter != s_GenericInstSet.end())
            return *iter;
    }

    Il2CppGenericInst* newInst = NULL;
    {
        il2cpp::os::FastAutoLock lock(&g_MetadataLock);
        newInst  = (Il2CppGenericInst*)MetadataMalloc(sizeof(Il2CppGenericInst));
        newInst->type_argc = typeCount;
        newInst->type_argv = (const Il2CppType**)MetadataMalloc(newInst->type_argc * sizeof(Il2CppType*));
    }

    index = 0;
    for (const Il2CppType* const* iter = types; iter != typesEnd; ++iter, ++index)
        newInst->type_argv[index] = *iter;

    {
        // Acquire lock agains to attempt to cache inst.
        il2cpp::os::FastAutoLock lock(&s_MetadataCache.m_CacheMutex);
        // Another thread may have already added this inst or we may be the first.
        // In either case, the iterator returned from 'insert' points to the item
        // cached within the set. We can always return this. In the case of another
        // thread beating us, the only downside is an extra allocation in the
        // metadata memory pool that lives for life of process anyway.
        auto result = s_GenericInstSet.insert(newInst);
        if (result.second)
            ++il2cpp_runtime_stats.generic_instance_count;

        return *(result.first);
    }
}

const Il2CppGenericInst* il2cpp::vm::MetadataCache::GetGenericInst(const il2cpp::metadata::Il2CppTypeVector& types)
{
    return GetGenericInst(&types[0], static_cast<uint32_t>(types.size()));
}

static il2cpp::os::FastMutex s_GenericMethodMutex;
const Il2CppGenericMethod* il2cpp::vm::MetadataCache::GetGenericMethod(const MethodInfo* methodDefinition, const Il2CppGenericInst* classInst, const Il2CppGenericInst* methodInst)
{
    Il2CppGenericMethod method = { 0 };
    method.methodDefinition = methodDefinition;
    method.context.class_inst = classInst;
    method.context.method_inst = methodInst;

    il2cpp::os::FastAutoLock lock(&s_GenericMethodMutex);
    Il2CppGenericMethodSet::const_iterator iter = s_GenericMethodSet.find(&method);
    if (iter != s_GenericMethodSet.end())
        return *iter;

    Il2CppGenericMethod* newMethod = MetadataAllocGenericMethod();
    newMethod->methodDefinition = methodDefinition;
    newMethod->context.class_inst = classInst;
    newMethod->context.method_inst = methodInst;

    s_GenericMethodSet.insert(newMethod);

    return newMethod;
}

static bool IsShareableEnum(const Il2CppType* type)
{
    // Base case for recursion - we've found an enum.
    if (il2cpp::vm::Type::IsEnum(type))
        return true;

    if (il2cpp::vm::Type::IsGenericInstance(type))
    {
        // Recursive case - look "inside" the generic instance type to see if this is a nested enum.
        Il2CppClass* definition = il2cpp::vm::GenericClass::GetTypeDefinition(type->data.generic_class);
        return IsShareableEnum(il2cpp::vm::Class::GetType(definition));
    }

    // Base case for recurion - this is not an enum or a generic instance type.
    return false;
}

// this logic must match the C# logic in GenericSharingAnalysis.GetSharedTypeForGenericParameter
static const Il2CppGenericInst* GetSharedInst(const Il2CppGenericInst* inst)
{
    if (inst == NULL)
        return NULL;

    il2cpp::metadata::Il2CppTypeVector types;
    for (uint32_t i = 0; i < inst->type_argc; ++i)
    {
        if (il2cpp::vm::Type::IsReference(inst->type_argv[i]))
            types.push_back(&il2cpp_defaults.object_class->byval_arg);
        else
        {
            const Il2CppType* type = inst->type_argv[i];
            if (s_Il2CppCodeGenOptions->enablePrimitiveValueTypeGenericSharing)
            {
                if (IsShareableEnum(type))
                {
                    const Il2CppType* underlyingType = il2cpp::vm::Type::GetUnderlyingType(type);
                    switch (underlyingType->type)
                    {
                        case IL2CPP_TYPE_I1:
                            type = &il2cpp_defaults.sbyte_shared_enum->byval_arg;
                            break;
                        case IL2CPP_TYPE_I2:
                            type = &il2cpp_defaults.int16_shared_enum->byval_arg;
                            break;
                        case IL2CPP_TYPE_I4:
                            type = &il2cpp_defaults.int32_shared_enum->byval_arg;
                            break;
                        case IL2CPP_TYPE_I8:
                            type = &il2cpp_defaults.int64_shared_enum->byval_arg;
                            break;
                        case IL2CPP_TYPE_U1:
                            type = &il2cpp_defaults.byte_shared_enum->byval_arg;
                            break;
                        case IL2CPP_TYPE_U2:
                            type = &il2cpp_defaults.uint16_shared_enum->byval_arg;
                            break;
                        case IL2CPP_TYPE_U4:
                            type = &il2cpp_defaults.uint32_shared_enum->byval_arg;
                            break;
                        case IL2CPP_TYPE_U8:
                            type = &il2cpp_defaults.uint64_shared_enum->byval_arg;
                            break;
                        default:
                            IL2CPP_ASSERT(0 && "Invalid enum underlying type");
                            break;
                    }
                }
            }

            if (il2cpp::vm::Type::IsGenericInstance(type))
            {
                const Il2CppGenericInst* sharedInst = GetSharedInst(type->data.generic_class->context.class_inst);
                Il2CppGenericClass* gklass = il2cpp::metadata::GenericMetadata::GetGenericClass(type->data.generic_class->type, sharedInst);
                Il2CppClass* klass = il2cpp::vm::GenericClass::GetClass(gklass);
                type = &klass->byval_arg;
            }
            types.push_back(type);
        }
    }

    const Il2CppGenericInst* sharedInst = il2cpp::vm::MetadataCache::GetGenericInst(types);

    return sharedInst;
}

InvokerMethod il2cpp::vm::MetadataCache::GetInvokerMethodPointer(const MethodInfo* methodDefinition, const Il2CppGenericContext* context)
{
    Il2CppGenericMethod method = { 0 };
    method.methodDefinition = const_cast<MethodInfo*>(methodDefinition);
    method.context.class_inst = context->class_inst;
    method.context.method_inst = context->method_inst;

    Il2CppMethodTableMapIter iter = s_MethodTableMap.find(&method);
    if (iter != s_MethodTableMap.end())
    {
        IL2CPP_ASSERT(iter->second->invokerIndex >= 0);
        if (static_cast<uint32_t>(iter->second->invokerIndex) < s_Il2CppCodeRegistration->invokerPointersCount)
            return s_Il2CppCodeRegistration->invokerPointers[iter->second->invokerIndex];
        return NULL;
    }
    // get the shared version if it exists
    method.context.class_inst = GetSharedInst(context->class_inst);
    method.context.method_inst = GetSharedInst(context->method_inst);

    iter = s_MethodTableMap.find(&method);
    if (iter != s_MethodTableMap.end())
    {
        IL2CPP_ASSERT(iter->second->invokerIndex >= 0);
        if (static_cast<uint32_t>(iter->second->invokerIndex) < s_Il2CppCodeRegistration->invokerPointersCount)
            return s_Il2CppCodeRegistration->invokerPointers[iter->second->invokerIndex];
        return NULL;
    }

    return NULL;
}

Il2CppMethodPointer il2cpp::vm::MetadataCache::GetMethodPointer(const MethodInfo* methodDefinition, const Il2CppGenericContext* context, bool adjustorThunk, bool methodPointer)
{
    Il2CppGenericMethod method = { 0 };
    method.methodDefinition = const_cast<MethodInfo*>(methodDefinition);
    method.context.class_inst = context->class_inst;
    method.context.method_inst = context->method_inst;

    Il2CppMethodTableMapIter iter = s_MethodTableMap.find(&method);
    if (iter != s_MethodTableMap.end())
    {
        IL2CPP_ASSERT(iter->second->invokerIndex >= 0);
        if (iter->second->adjustorThunkIndex != -1 && adjustorThunk)
            return s_Il2CppCodeRegistration->genericAdjustorThunks[iter->second->adjustorThunkIndex];

        if (static_cast<uint32_t>(iter->second->methodIndex) < s_Il2CppCodeRegistration->genericMethodPointersCount && methodPointer)
            return s_Il2CppCodeRegistration->genericMethodPointers[iter->second->methodIndex];
        return NULL;
    }

    method.context.class_inst = GetSharedInst(context->class_inst);
    method.context.method_inst = GetSharedInst(context->method_inst);

    iter = s_MethodTableMap.find(&method);
    if (iter != s_MethodTableMap.end())
    {
        IL2CPP_ASSERT(iter->second->invokerIndex >= 0);
        if (iter->second->adjustorThunkIndex != -1 && adjustorThunk)
            return s_Il2CppCodeRegistration->genericAdjustorThunks[iter->second->adjustorThunkIndex];

        if (static_cast<uint32_t>(iter->second->methodIndex) < s_Il2CppCodeRegistration->genericMethodPointersCount && methodPointer)
            return s_Il2CppCodeRegistration->genericMethodPointers[iter->second->methodIndex];
        return NULL;
    }

    return NULL;
}

Il2CppClass* il2cpp::vm::MetadataCache::GetTypeInfoFromTypeIndex(TypeIndex index, bool throwOnError)
{
    if (index == kTypeIndexInvalid)
        return NULL;
    
    if (hybridclr::metadata::IsInterpreterIndex(index))
    {
        Il2CppClass* klass = il2cpp::vm::Class::FromIl2CppType(hybridclr::metadata::MetadataModule::GetIl2CppTypeFromEncodeIndex(index));
        ClassInlines::InitFromCodegen(klass);
        return klass;
    }

    IL2CPP_ASSERT(index < s_Il2CppMetadataRegistration->typesCount && "Invalid type index ");

    if (s_TypeInfoTable[index])
        return s_TypeInfoTable[index];

    const Il2CppType* type = s_Il2CppMetadataRegistration->types[index];
    Il2CppClass *klass = il2cpp::vm::Class::FromIl2CppType(type, throwOnError);
    if (klass)
    {
        il2cpp::vm::ClassInlines::InitFromCodegen(klass);
        s_TypeInfoTable[index] = klass;
    }

    return s_TypeInfoTable[index];
}

const Il2CppType* il2cpp::vm::MetadataCache::GetIl2CppTypeFromIndex(TypeIndex index)
{
    if (index == kTypeIndexInvalid)
        return NULL;
    
    if (hybridclr::metadata::IsInterpreterIndex(index))
    {
        return hybridclr::metadata::MetadataModule::GetIl2CppTypeFromEncodeIndex(index);
    }

    IL2CPP_ASSERT(index < s_Il2CppMetadataRegistration->typesCount && "Invalid type index ");

    return s_Il2CppMetadataRegistration->types[index];
}

const MethodInfo* il2cpp::vm::MetadataCache::GetMethodInfoFromIndex(EncodedMethodIndex methodIndex)
{
    uint32_t index = GetDecodedMethodIndex(methodIndex);

    if (index == 0)
        return NULL;

    if (GetEncodedIndexType(methodIndex) == kIl2CppMetadataUsageMethodRef)
        return il2cpp::metadata::GenericMethod::GetMethod(GetGenericMethodFromIndex(index));
    else
        return il2cpp::vm::MetadataCache::GetMethodInfoFromMethodDefinitionIndex(index);
}

const Il2CppGenericMethod* il2cpp::vm::MetadataCache::GetGenericMethodFromIndex(GenericMethodIndex index)
{
    IL2CPP_ASSERT(index < s_Il2CppMetadataRegistration->methodSpecsCount);
    if (s_GenericMethodTable[index])
        return s_GenericMethodTable[index];

    const Il2CppMethodSpec* methodSpec = s_Il2CppMetadataRegistration->methodSpecs + index;
    const MethodInfo* methodDefinition = GetMethodInfoFromMethodDefinitionIndex(methodSpec->methodDefinitionIndex);
    const Il2CppGenericInst* classInst = NULL;
    const Il2CppGenericInst* methodInst = NULL;
    if (methodSpec->classIndexIndex != -1)
    {
        IL2CPP_ASSERT(methodSpec->classIndexIndex < s_Il2CppMetadataRegistration->genericInstsCount);
        classInst = s_Il2CppMetadataRegistration->genericInsts[methodSpec->classIndexIndex];
    }
    if (methodSpec->methodIndexIndex != -1)
    {
        IL2CPP_ASSERT(methodSpec->methodIndexIndex < s_Il2CppMetadataRegistration->genericInstsCount);
        methodInst = s_Il2CppMetadataRegistration->genericInsts[methodSpec->methodIndexIndex];
    }
    s_GenericMethodTable[index] = GetGenericMethod(methodDefinition, classInst, methodInst);

    return s_GenericMethodTable[index];
}

static int CompareIl2CppTokenAdjustorThunkPair(const void* pkey, const void* pelem)
{
    return (int)(((Il2CppTokenAdjustorThunkPair*)pkey)->token - ((Il2CppTokenAdjustorThunkPair*)pelem)->token);
}

Il2CppMethodPointer il2cpp::vm::MetadataCache::GetAdjustorThunk(const Il2CppImage* image, uint32_t token)
{
    if (hybridclr::metadata::IsInterpreterIndex(image->token))
    {
        return hybridclr::metadata::MetadataModule::GetAdjustorThunk(image, token);
    }
    if (image->codeGenModule->adjustorThunkCount == 0)
        return NULL;

    Il2CppTokenAdjustorThunkPair key;
    memset(&key, 0, sizeof(Il2CppTokenAdjustorThunkPair));
    key.token = token;

    const Il2CppTokenAdjustorThunkPair* result = (const Il2CppTokenAdjustorThunkPair*)bsearch(&key, image->codeGenModule->adjustorThunks,
        image->codeGenModule->adjustorThunkCount, sizeof(Il2CppTokenAdjustorThunkPair), CompareIl2CppTokenAdjustorThunkPair);

    if (result == NULL)
        return NULL;

    return result->adjustorThunk;
}

Il2CppMethodPointer il2cpp::vm::MetadataCache::GetMethodPointer(const Il2CppImage* image, uint32_t token)
{
    uint32_t rid = GetTokenRowId(token);
    uint32_t table =  GetTokenType(token);
    if (rid == 0)
        return NULL;

    if (hybridclr::metadata::IsInterpreterImage(image))
    {
        return hybridclr::metadata::MetadataModule::GetMethodPointer(image, token);
    }

    IL2CPP_ASSERT(rid <= image->codeGenModule->methodPointerCount);

    return image->codeGenModule->methodPointers[rid - 1];
}

InvokerMethod il2cpp::vm::MetadataCache::GetMethodInvoker(const Il2CppImage* image, uint32_t token)
{
    uint32_t rid = GetTokenRowId(token);
    uint32_t table = GetTokenType(token);
    if (rid == 0)
        return NULL;
    if (hybridclr::metadata::IsInterpreterImage(image))
    {
        return hybridclr::metadata::MetadataModule::GetMethodInvoker(image, token);
    }
    int32_t index = image->codeGenModule->invokerIndices[rid - 1];

    if (index == kMethodIndexInvalid)
        return NULL;

    IL2CPP_ASSERT(index >= 0 && static_cast<uint32_t>(index) < s_Il2CppCodeRegistration->invokerPointersCount);
    return s_Il2CppCodeRegistration->invokerPointers[index];
}

const Il2CppInteropData* il2cpp::vm::MetadataCache::GetInteropDataForType(const Il2CppType* type)
{
    IL2CPP_ASSERT(type != NULL);
    InteropDataMap::iterator interopData = s_InteropData.find_first(type);
    if (interopData == s_InteropData.end())
        return NULL;

    return interopData;
}

static bool MatchTokens(Il2CppTokenIndexMethodTuple key, Il2CppTokenIndexMethodTuple element)
{
    return key.token < element.token;
}

Il2CppMethodPointer il2cpp::vm::MetadataCache::GetReversePInvokeWrapper(const Il2CppImage* image, const MethodInfo* method)
{
    if (hybridclr::metadata::IsInterpreterImage(image))
    {
        return hybridclr::metadata::MetadataModule::GetReversePInvokeWrapper(image, method);
    }
    if (image->codeGenModule->reversePInvokeWrapperCount == 0)
        return NULL;

    // For each image (i.e. assembly), the reverse pinvoke wrapper indices are in an array sorted by
    // metadata token. Each entry also might have the method metadata pointer, which is used to further
    // find methods that have a matching metadata token.

    Il2CppTokenIndexMethodTuple key;
    memset(&key, 0, sizeof(Il2CppTokenIndexMethodTuple));
    key.token = method->token;

    // Binary search for a range which matches the metadata token.
    auto begin = image->codeGenModule->reversePInvokeWrapperIndices;
    auto end = image->codeGenModule->reversePInvokeWrapperIndices + image->codeGenModule->reversePInvokeWrapperCount;
    auto matchingRange = std::equal_range(begin, end, key, &MatchTokens);

    int32_t index = -1;
    auto numberOfMatches = std::distance(matchingRange.first, matchingRange.second);
    if (numberOfMatches == 1)
    {
        // Normal case - we found one non-generic method.
        index = matchingRange.first->index;
    }
    else if (numberOfMatches > 1)
    {
        // Multiple generic instance methods share the same token, since it is from the generic method definition.
        // To find the proper method, look for the one with a matching method metadata pointer.
        const Il2CppTokenIndexMethodTuple* currentMatch = matchingRange.first;
        const Il2CppTokenIndexMethodTuple* lastMatch = matchingRange.second;
        while (currentMatch != lastMatch)
        {
            // First, check the method metadata, and use it if it has been initialized.
            // If not, let's fall back to the generic method.
            const MethodInfo* possibleMatch = (const MethodInfo*)*currentMatch->method;
            if (possibleMatch == NULL)
                possibleMatch = il2cpp::metadata::GenericMethod::GetMethod(GetGenericMethodFromIndex(currentMatch->genericMethodIndex));
            if (possibleMatch == method)
            {
                index = currentMatch->index;
                break;
            }
            currentMatch++;
        }
    }

    if (index == -1)
        return NULL;

    IL2CPP_ASSERT(index >= 0 && static_cast<uint32_t>(index) < s_Il2CppCodeRegistration->reversePInvokeWrapperCount);
    return s_Il2CppCodeRegistration->reversePInvokeWrappers[index];
}

static const Il2CppType* GetReducedType(const Il2CppType* type)
{
    if (type->byref)
        return &il2cpp_defaults.object_class->byval_arg;

    if (il2cpp::vm::Type::IsEnum(type))
        type = il2cpp::vm::Type::GetUnderlyingType(type);

    switch (type->type)
    {
        case IL2CPP_TYPE_BOOLEAN:
            return &il2cpp_defaults.sbyte_class->byval_arg;
        case IL2CPP_TYPE_CHAR:
            return &il2cpp_defaults.int16_class->byval_arg;
        case IL2CPP_TYPE_BYREF:
        case IL2CPP_TYPE_CLASS:
        case IL2CPP_TYPE_OBJECT:
        case IL2CPP_TYPE_STRING:
        case IL2CPP_TYPE_ARRAY:
        case IL2CPP_TYPE_SZARRAY:
            return &il2cpp_defaults.object_class->byval_arg;
        case IL2CPP_TYPE_GENERICINST:
            if (il2cpp::vm::Type::GenericInstIsValuetype(type))
                return type;
            else
                return &il2cpp_defaults.object_class->byval_arg;
        default:
            return type;
    }
}

Il2CppMethodPointer il2cpp::vm::MetadataCache::GetUnresolvedVirtualCallStub(const MethodInfo* method)
{
    il2cpp::utils::dynamic_array<const Il2CppType*> signature;

    signature.push_back(GetReducedType(method->return_type));
    for (int i = 0; i < method->parameters_count; ++i)
        signature.push_back(GetReducedType(method->parameters[i].parameter_type));

    Il2CppUnresolvedSignatureMapIter it = s_pUnresolvedSignatureMap->find(signature);
    if (it != s_pUnresolvedSignatureMap->end())
        return it->second;

    return NULL;
}

static const Il2CppImage* GetImageForTypeDefinitionIndex(TypeDefinitionIndex index)
{
    if (hybridclr::metadata::IsInterpreterIndex(index))
    {
        return hybridclr::metadata::MetadataModule::GetImage(hybridclr::metadata::DecodeImageIndex(index))->GetIl2CppImage();
    }
    for (int32_t imageIndex = 0; imageIndex < s_ImagesCount; imageIndex++)
    {
        const Il2CppImage* image = s_ImagesTable + imageIndex;
        IL2CPP_ASSERT(index >= 0);
        if (index >= image->typeStart && static_cast<uint32_t>(index) < (image->typeStart + image->typeCount))
            return image;
    }

    IL2CPP_ASSERT(0 && "Failed to find owning image for type");
    return NULL;
}

static uint8_t ConvertPackingSizeEnumToValue(PackingSize packingSize)
{
    switch (packingSize)
    {
        case Zero:
            return 0;
        case One:
            return 1;
        case Two:
            return 2;
        case Four:
            return 4;
        case Eight:
            return 8;
        case Sixteen:
            return 16;
        case ThirtyTwo:
            return 32;
        case SixtyFour:
            return 64;
        case OneHundredTwentyEight:
            return 128;
        default:
            Assert(0 && "Invalid packing size!");
            return 0;
    }
}

int32_t il2cpp::vm::MetadataCache::StructLayoutPack(TypeDefinitionIndex index)
{
    const Il2CppTypeDefinition* typeDefinition = GetTypeDefinitionFromIndex(index);
    return ConvertPackingSizeEnumToValue(static_cast<PackingSize>((typeDefinition->bitfield >> (kSpecifiedPackingSize - 1)) & 0xF));
}

bool il2cpp::vm::MetadataCache::StructLayoutPackIsDefault(TypeDefinitionIndex index)
{
    const Il2CppTypeDefinition* typeDefinition = GetTypeDefinitionFromIndex(index);
    return (typeDefinition->bitfield >> (kPackingSizeIsDefault - 1)) & 0x1;
}

bool il2cpp::vm::MetadataCache::StructLayoutSizeIsDefault(TypeDefinitionIndex index)
{
    const Il2CppTypeDefinition* typeDefinition = GetTypeDefinitionFromIndex(index);
    return (typeDefinition->bitfield >> (kClassSizeIsDefault - 1)) & 0x1;
}

Il2CppClass* il2cpp::vm::MetadataCache::FromTypeDefinition(TypeDefinitionIndex index)
{
    const Il2CppTypeDefinition* typeDefinition;
    const Il2CppTypeDefinitionSizes* typeDefinitionSizes;
    if (hybridclr::metadata::IsInterpreterIndex(index))
    {
        typeDefinition = (const Il2CppTypeDefinition*)hybridclr::metadata::MetadataModule::GetAssemblyTypeHandleFromEncodeIndex(index);
        typeDefinitionSizes = hybridclr::metadata::MetadataModule::GetTypeDefinitionSizesFromEncodeIndex(index);
    }
    else
    {
        IL2CPP_ASSERT(index >= 0 && static_cast<uint32_t>(index) < s_GlobalMetadataHeader->typeDefinitionsCount / sizeof(Il2CppTypeDefinition));
        const Il2CppTypeDefinition* typeDefinitions = (const Il2CppTypeDefinition*)((const char*)s_GlobalMetadata + s_GlobalMetadataHeader->typeDefinitionsOffset);
        typeDefinition = typeDefinitions + index;
        typeDefinitionSizes = s_Il2CppMetadataRegistration->typeDefinitionsSizes[index];
    }
    Il2CppClass* typeInfo = (Il2CppClass*)IL2CPP_CALLOC(1, sizeof(Il2CppClass) + (sizeof(VirtualInvokeData) * typeDefinition->vtable_count));
    typeInfo->klass = typeInfo;
    typeInfo->image = GetImageForTypeDefinitionIndex(index);
    typeInfo->name = il2cpp::vm::MetadataCache::GetStringFromIndex(typeDefinition->nameIndex);
    typeInfo->namespaze = il2cpp::vm::MetadataCache::GetStringFromIndex(typeDefinition->namespaceIndex);
    typeInfo->byval_arg = *il2cpp::vm::MetadataCache::GetIl2CppTypeFromIndex(typeDefinition->byvalTypeIndex);
    typeInfo->this_arg = *il2cpp::vm::MetadataCache::GetIl2CppTypeFromIndex(typeDefinition->byrefTypeIndex);
    typeInfo->typeDefinition = typeDefinition;
    typeInfo->genericContainerIndex = typeDefinition->genericContainerIndex;
    typeInfo->instance_size = typeDefinitionSizes->instance_size;
    typeInfo->actualSize = typeDefinitionSizes->instance_size; // actualySize is instance_size for compiler generated values
    typeInfo->native_size = typeDefinitionSizes->native_size;
    typeInfo->static_fields_size = typeDefinitionSizes->static_fields_size;
    typeInfo->thread_static_fields_size = typeDefinitionSizes->thread_static_fields_size;
    typeInfo->thread_static_fields_offset = -1;
    typeInfo->flags = typeDefinition->flags;
    typeInfo->valuetype = (typeDefinition->bitfield >> (kBitIsValueType - 1)) & 0x1;
    typeInfo->enumtype = (typeDefinition->bitfield >> (kBitIsEnum - 1)) & 0x1;
    typeInfo->is_generic = typeDefinition->genericContainerIndex != kGenericContainerIndexInvalid; // generic if we have a generic container
    typeInfo->has_finalize = (typeDefinition->bitfield >> (kBitHasFinalizer - 1)) & 0x1;
    typeInfo->has_cctor = (typeDefinition->bitfield >> (kBitHasStaticConstructor - 1)) & 0x1;
    typeInfo->is_blittable = (typeDefinition->bitfield >> (kBitIsBlittable - 1)) & 0x1;
    typeInfo->is_import_or_windows_runtime = (typeDefinition->bitfield >> (kBitIsImportOrWindowsRuntime - 1)) & 0x1;
    typeInfo->packingSize = ConvertPackingSizeEnumToValue(static_cast<PackingSize>((typeDefinition->bitfield >> (kPackingSize - 1)) & 0xF));
    typeInfo->method_count = typeDefinition->method_count;
    typeInfo->property_count = typeDefinition->property_count;
    typeInfo->field_count = typeDefinition->field_count;
    typeInfo->event_count = typeDefinition->event_count;
    typeInfo->nested_type_count = typeDefinition->nested_type_count;
    typeInfo->vtable_count = typeDefinition->vtable_count;
    typeInfo->interfaces_count = typeDefinition->interfaces_count;
    typeInfo->interface_offsets_count = typeDefinition->interface_offsets_count;
    typeInfo->token = typeDefinition->token;
    typeInfo->interopData = il2cpp::vm::MetadataCache::GetInteropDataForType(&typeInfo->byval_arg);

    if (typeDefinition->parentIndex != kTypeIndexInvalid)
        typeInfo->parent = il2cpp::vm::Class::FromIl2CppType(il2cpp::vm::MetadataCache::GetIl2CppTypeFromIndex(typeDefinition->parentIndex));

    if (typeDefinition->declaringTypeIndex != kTypeIndexInvalid)
        typeInfo->declaringType = il2cpp::vm::Class::FromIl2CppType(il2cpp::vm::MetadataCache::GetIl2CppTypeFromIndex(typeDefinition->declaringTypeIndex));

    typeInfo->castClass = typeInfo->element_class = typeInfo;
    if (typeInfo->enumtype)
        typeInfo->castClass = typeInfo->element_class = il2cpp::vm::Class::FromIl2CppType(il2cpp::vm::MetadataCache::GetIl2CppTypeFromIndex(typeDefinition->elementTypeIndex));

    return typeInfo;
}

const Il2CppAssembly* il2cpp::vm::MetadataCache::GetAssemblyFromIndex(const Il2CppImage* image, AssemblyIndex index)
{
    if (hybridclr::metadata::IsInterpreterIndex(index))
    {
        return (Il2CppAssembly*)hybridclr::metadata::MetadataModule::GetAssemblyTypeHandleFromRawIndex(image, index);
    }

    if (index == kGenericContainerIndexInvalid)
        return NULL;

    IL2CPP_ASSERT(index <= s_AssembliesCount);
    return s_AssembliesTable + index;
}

const Il2CppAssembly* il2cpp::vm::MetadataCache::GetAssemblyByName(const char* nameToFind)
{
    const char* assemblyName = hybridclr::GetAssemblyNameFromPath(nameToFind);

    il2cpp::utils::VmStringUtils::CaseInsensitiveComparer comparer;

    for (int i = 0; i < s_AssembliesCount; i++)
    {
        const Il2CppAssembly* assembly = s_AssembliesTable + i;

        if (comparer(assembly->aname.name, assemblyName) || comparer(assembly->image->name, assemblyName))
            return assembly;
    }

    il2cpp::os::FastAutoLock lock(&il2cpp::vm::g_MetadataLock);

    for (auto assembly : s_cliAssemblies)
    {
        if (comparer(assembly->aname.name, assemblyName) || comparer(assembly->image->name, assemblyName))
            return assembly;
    }

    return nullptr;
}

void il2cpp::vm::MetadataCache::RegisterInterpreterAssembly(Il2CppAssembly* assembly)
{
    il2cpp::vm::Assembly::Register(assembly);
    s_cliAssemblies.push_back(assembly);
}

const Il2CppAssembly* il2cpp::vm::MetadataCache::LoadAssemblyFromBytes(const char* assemblyBytes, size_t length)
{
    il2cpp::os::FastAutoLock lock(&il2cpp::vm::g_MetadataLock);

    Il2CppAssembly* newAssembly = hybridclr::metadata::Assembly::LoadFromBytes(assemblyBytes, length, true);
    // avoid register placeholder assembly twicely.
    for (Il2CppAssembly* ass : s_cliAssemblies)
    {
        if (ass == newAssembly)
        {
            il2cpp::vm::Assembly::InvalidateAssemblyList();
            return ass;
        }
    }
    RegisterInterpreterAssembly(newAssembly);

    return newAssembly;
}

const Il2CppGenericMethod* il2cpp::vm::MetadataCache::FindGenericMethod(std::function<bool(const Il2CppGenericMethod*)> predic)
{
    for (auto e : s_MethodTableMap)
    {
        if (predic(e.first))
        {
            return e.first;
        }
    }
    return nullptr;
}

void il2cpp::vm::MetadataCache::FixThreadLocalStaticOffsetForFieldLocked(FieldInfo* field, int32_t offset, const il2cpp::os::FastAutoLock& lock)
{
    s_ThreadLocalStaticOffsetMap[field] = offset;
}

Il2CppImage* il2cpp::vm::MetadataCache::GetImageFromIndex(ImageIndex index)
{
    if (index == kGenericContainerIndexInvalid)
        return NULL;

    IL2CPP_ASSERT(index <= s_ImagesCount);
    return s_ImagesTable + index;
}

Il2CppClass* il2cpp::vm::MetadataCache::GetTypeInfoFromTypeDefinitionIndex(TypeDefinitionIndex index)
{
    if (index == kTypeIndexInvalid)
        return NULL;

    if (hybridclr::metadata::IsInterpreterIndex(index))
    {
        return hybridclr::metadata::MetadataModule::GetTypeInfoFromTypeDefinitionEncodeIndex(index);
    }

    IL2CPP_ASSERT(index >= 0 && static_cast<uint32_t>(index) < s_GlobalMetadataHeader->typeDefinitionsCount / sizeof(Il2CppTypeDefinition));

    if (!s_TypeInfoDefinitionTable[index])
    {
        // we need to use the metadata lock, since we may need to retrieve other Il2CppClass's when setting. Our parent may be a generic instance for example
        il2cpp::os::FastAutoLock lock(&g_MetadataLock);
        // double checked locking
        if (!s_TypeInfoDefinitionTable[index])
            s_TypeInfoDefinitionTable[index] = FromTypeDefinition(index);
    }

    return s_TypeInfoDefinitionTable[index];
}

const Il2CppTypeDefinition* il2cpp::vm::MetadataCache::GetTypeDefinitionFromIndex(TypeDefinitionIndex index)
{
    if (hybridclr::metadata::IsInterpreterIndex(index))
    {
        return (const Il2CppTypeDefinition*)hybridclr::metadata::MetadataModule::GetAssemblyTypeHandleFromEncodeIndex(index);
    }

    if (index == kTypeDefinitionIndexInvalid)
        return NULL;

    IL2CPP_ASSERT(index >= 0 && static_cast<uint32_t>(index) < s_GlobalMetadataHeader->typeDefinitionsCount / sizeof(Il2CppTypeDefinition));
    const Il2CppTypeDefinition* typeDefinitions = (const Il2CppTypeDefinition*)((const char*)s_GlobalMetadata + s_GlobalMetadataHeader->typeDefinitionsOffset);
    return typeDefinitions + index;
}

TypeDefinitionIndex il2cpp::vm::MetadataCache::GetExportedTypeFromIndex(TypeDefinitionIndex index)
{
    if (hybridclr::metadata::IsInterpreterIndex(index))
    {
        return ((Il2CppTypeDefinition*) hybridclr::metadata::MetadataModule::GetAssemblyExportedTypeHandleFromEncodeIndex(index))->byvalTypeIndex;
    }
    if (index == kTypeDefinitionIndexInvalid)
        return kTypeDefinitionIndexInvalid;
    IL2CPP_ASSERT(index >= 0 && static_cast<uint32_t>(index) < s_GlobalMetadataHeader->exportedTypeDefinitionsCount / sizeof(TypeDefinitionIndex));
    TypeDefinitionIndex* exportedTypes = (TypeDefinitionIndex*)((const char*)s_GlobalMetadata + s_GlobalMetadataHeader->exportedTypeDefinitionsOffset);
    return *(exportedTypes + index);
}

const Il2CppGenericContainer* il2cpp::vm::MetadataCache::GetGenericContainerFromIndex(GenericContainerIndex index)
{
    if (index == kGenericContainerIndexInvalid)
        return NULL;
    if (hybridclr::metadata::IsInterpreterIndex(index))
    {
        return hybridclr::metadata::MetadataModule::GetGenericContainerFromEncodeIndex(index);
    }
    IL2CPP_ASSERT(index >= 0 && static_cast<uint32_t>(index) <= s_GlobalMetadataHeader->genericContainersCount / sizeof(Il2CppGenericContainer));
    const Il2CppGenericContainer* genericContainers = (const Il2CppGenericContainer*)((const char*)s_GlobalMetadata + s_GlobalMetadataHeader->genericContainersOffset);
    return genericContainers + index;
}

const Il2CppGenericParameter* il2cpp::vm::MetadataCache::GetGenericParameterFromIndex(GenericParameterIndex index)
{
    if (index == kGenericParameterIndexInvalid)
        return NULL;

    if (hybridclr::metadata::IsInterpreterIndex(index))
    {
        return hybridclr::metadata::MetadataModule::GetImageByEncodedIndex(index)
            ->GetGenericParameterByGlobalIndex(hybridclr::metadata::DecodeMetadataIndex(index));
    }
    IL2CPP_ASSERT(index >= 0 && static_cast<uint32_t>(index) <= s_GlobalMetadataHeader->genericParametersCount / sizeof(Il2CppGenericParameter));
    const Il2CppGenericParameter* genericParameters = (const Il2CppGenericParameter*)((const char*)s_GlobalMetadata + s_GlobalMetadataHeader->genericParametersOffset);
    return genericParameters + index;
}

const Il2CppType* il2cpp::vm::MetadataCache::GetGenericParameterConstraintFromIndex(GenericParameterConstraintIndex index)
{
    if (hybridclr::metadata::IsInterpreterIndex(index))
    {
        return hybridclr::metadata::MetadataModule::GetImageByEncodedIndex(index)
            ->GetGenericParameterConstraintFromIndex(hybridclr::metadata::DecodeMetadataIndex(index));
    }

    IL2CPP_ASSERT(index >= 0 && static_cast<uint32_t>(index) <= s_GlobalMetadataHeader->genericParameterConstraintsCount / sizeof(TypeIndex));
    const TypeIndex* constraintIndices = (const TypeIndex*)((const char*)s_GlobalMetadata + s_GlobalMetadataHeader->genericParameterConstraintsOffset);

    return GetIl2CppTypeFromIndex(constraintIndices[index]);
}

Il2CppClass* il2cpp::vm::MetadataCache::GetNestedTypeFromOffset(const Il2CppTypeDefinition* typeDefinition, NestedTypeIndex offset)
{
    if (hybridclr::metadata::IsInterpreterType(typeDefinition))
    {
        return hybridclr::metadata::MetadataModule::GetNestedTypeFromOffset(typeDefinition, offset);
    }
    NestedTypeIndex index = typeDefinition->nestedTypesStart + offset;
    IL2CPP_ASSERT(index >= 0 && static_cast<uint32_t>(index) <= s_GlobalMetadataHeader->nestedTypesCount / sizeof(TypeDefinitionIndex));
    const TypeDefinitionIndex* nestedTypeIndices = (const TypeDefinitionIndex*)((const char*)s_GlobalMetadata + s_GlobalMetadataHeader->nestedTypesOffset);

    return GetTypeInfoFromTypeDefinitionIndex(nestedTypeIndices[index]);
}

const Il2CppType* il2cpp::vm::MetadataCache::GetInterfaceFromIndex(Il2CppClass* klass, InterfacesIndex index)
{
    if (hybridclr::metadata::IsInterpreterType(klass))
    {
        return hybridclr::metadata::MetadataModule::GetInterfaceFromIndex(klass, index);
    }
    IL2CPP_ASSERT(index >= 0 && static_cast<uint32_t>(index) <= s_GlobalMetadataHeader->interfacesCount / sizeof(TypeIndex));
    const TypeIndex* interfaceIndices = (const TypeIndex*)((const char*)s_GlobalMetadata + s_GlobalMetadataHeader->interfacesOffset);

    return GetIl2CppTypeFromIndex(interfaceIndices[index]);
}

EncodedMethodIndex il2cpp::vm::MetadataCache::GetVTableMethodFromIndex(VTableIndex index)
{
    IL2CPP_ASSERT(index >= 0 && static_cast<uint32_t>(index) <= s_GlobalMetadataHeader->vtableMethodsCount / sizeof(EncodedMethodIndex));
    const EncodedMethodIndex* methodReferences = (const EncodedMethodIndex*)((const char*)s_GlobalMetadata + s_GlobalMetadataHeader->vtableMethodsOffset);

    return methodReferences[index];
}

const MethodInfo* il2cpp::vm::MetadataCache::GetMethodInfoFromVTableSlot(const Il2CppClass* klass, int32_t vTableSlot)
{
    if (hybridclr::metadata::IsInterpreterType(klass))
    {
        return hybridclr::metadata::MetadataModule::GetMethodInfoFromVTableSlot(klass, vTableSlot);
    }
    EncodedMethodIndex vtableMethodIndex = GetVTableMethodFromIndex(klass->typeDefinition->vtableStart + vTableSlot);
    return MetadataCache::GetMethodInfoFromIndex(vtableMethodIndex);
}

const MethodInfo* il2cpp::vm::MetadataCache::GetMethodInfoFromMethodHandle(Il2CppMetadataMethodDefinitionHandle handle)
{
    if (hybridclr::metadata::IsInterpreterMethod(handle))
    {
        return hybridclr::metadata::MetadataModule::GetMethodInfoFromMethodDefinition(handle);
    }

    const Il2CppMethodDefinition* methods = (const Il2CppMethodDefinition*)((const char*)s_GlobalMetadata + s_GlobalMetadataHeader->methodsOffset);
    int32_t index = (int32_t)(handle - methods);
    IL2CPP_ASSERT(index >= 0 && static_cast<uint32_t>(index) <= s_GlobalMetadataHeader->methodsCount / sizeof(Il2CppMethodDefinition));

    if (!s_MethodInfoDefinitionTable[index])
    {
        Il2CppClass* typeInfo = GetTypeInfoFromTypeDefinitionIndex(handle->declaringType);
        il2cpp::vm::Class::SetupMethods(typeInfo);
        s_MethodInfoDefinitionTable[index] = typeInfo->methods[index - typeInfo->typeDefinition->methodStart];
    }

    return s_MethodInfoDefinitionTable[index];
}

// hybridclr并没有这个接口，新增也比较麻烦，正好这个接口当前使用方式为调用获得offsetPair后再接着获得OffsetInfo，
// 索性直接调用获得OffsetInfo的代码
//Il2CppInterfaceOffsetPair il2cpp::vm::MetadataCache::GetInterfaceOffsetIndex(InterfaceOffsetIndex index)
//{
//    if (hybridclr::metadata::IsInterpreterIndex(index))
//    {
//        return hybridclr::metadata::MetadataModule::GetInterfaceOffsetIndex(typeDefinition, index);
//    }
//    IL2CPP_ASSERT(index >= 0 && static_cast<uint32_t>(index) <= s_GlobalMetadataHeader->interfaceOffsetsCount / sizeof(Il2CppInterfaceOffsetPair));
//    const Il2CppInterfaceOffsetPair* interfaceOffsets = (const Il2CppInterfaceOffsetPair*)((const char*)s_GlobalMetadata + s_GlobalMetadataHeader->interfaceOffsetsOffset);
//
//    return interfaceOffsets[index];
//}

Il2CppInterfaceOffsetInfo il2cpp::vm::MetadataCache::GetInterfaceOffsetInfo(const Il2CppTypeDefinition* typeDefinition, TypeInterfaceOffsetIndex offset)
{
    if (hybridclr::metadata::IsInterpreterType(typeDefinition))
    {
        return hybridclr::metadata::MetadataModule::GetInterfaceOffsetInfo(typeDefinition, offset);
    }
    TypeInterfaceOffsetIndex index = typeDefinition->interfaceOffsetsStart + offset;
    IL2CPP_ASSERT(index >= 0 && static_cast<uint32_t>(index) <= s_GlobalMetadataHeader->interfaceOffsetsCount / sizeof(Il2CppInterfaceOffsetPair));
    const Il2CppInterfaceOffsetPair* interfaceOffsets = (const Il2CppInterfaceOffsetPair*)((const char*)s_GlobalMetadata + s_GlobalMetadataHeader->interfaceOffsetsOffset);

    const Il2CppInterfaceOffsetPair& interfaceOffsetIndex = interfaceOffsets[index];
    return {
        GetIl2CppTypeFromIndex(interfaceOffsetIndex.interfaceTypeIndex),
        interfaceOffsetIndex.offset,
    };
}

static int CompareIl2CppTokenRangePair(const void* pkey, const void* pelem)
{
    return (int)(((Il2CppTokenRangePair*)pkey)->token - ((Il2CppTokenRangePair*)pelem)->token);
}

il2cpp::vm::RGCTXCollection il2cpp::vm::MetadataCache::GetRGCTXs(const Il2CppImage* image, uint32_t token)
{
    il2cpp::vm::RGCTXCollection collection = { 0, NULL };
    if (image->codeGenModule->rgctxRangesCount == 0)
        return collection;

    Il2CppTokenRangePair key;
    memset(&key, 0, sizeof(Il2CppTokenRangePair));
    key.token = token;

    const Il2CppTokenRangePair* res = (const Il2CppTokenRangePair*)bsearch(&key, image->codeGenModule->rgctxRanges, image->codeGenModule->rgctxRangesCount, sizeof(Il2CppTokenRangePair), CompareIl2CppTokenRangePair);

    if (res == NULL)
        return collection;

    collection.count = res->range.length;
    collection.items = image->codeGenModule->rgctxs + res->range.start;

    return collection;
}

const Il2CppEventDefinition* il2cpp::vm::MetadataCache::GetEventDefinitionFromIndex(EventIndex index)
{
    if (hybridclr::metadata::IsInterpreterIndex(index))
    {
        return hybridclr::metadata::MetadataModule::GetImageByEncodedIndex(index)->GetEventDefinitionFromIndex(hybridclr::metadata::DecodeMetadataIndex(index));
    }
    IL2CPP_ASSERT(index >= 0 && static_cast<uint32_t>(index) <= s_GlobalMetadataHeader->eventsCount / sizeof(Il2CppEventDefinition));
    const Il2CppEventDefinition* events = (const Il2CppEventDefinition*)((const char*)s_GlobalMetadata + s_GlobalMetadataHeader->eventsOffset);
    return events + index;
}

const Il2CppFieldDefinition* il2cpp::vm::MetadataCache::GetFieldDefinitionFromIndex(FieldIndex index)
{
    if (hybridclr::metadata::IsInterpreterIndex(index))
    {
        return hybridclr::metadata::MetadataModule::GetFieldDefinitionFromEncodeIndex(index);
    }
    IL2CPP_ASSERT(index >= 0 && static_cast<uint32_t>(index) <= s_GlobalMetadataHeader->fieldsCount / sizeof(Il2CppFieldDefinition));
    const Il2CppFieldDefinition* fields = (const Il2CppFieldDefinition*)((const char*)s_GlobalMetadata + s_GlobalMetadataHeader->fieldsOffset);
    return fields + index;
}

const Il2CppFieldDefaultValue* il2cpp::vm::MetadataCache::GetFieldDefaultValueFromIndex(FieldIndex index)
{
    if (hybridclr::metadata::IsInterpreterIndex((uint32_t)index))
    {
        return hybridclr::metadata::MetadataModule::GetFieldDefaultValueEntry((uint32_t)index);
    }
    IL2CPP_ASSERT(index >= 0 && static_cast<uint32_t>(index) <= s_GlobalMetadataHeader->fieldDefaultValuesCount / sizeof(Il2CppFieldDefaultValue));
    const Il2CppFieldDefaultValue* defaultValues = (const Il2CppFieldDefaultValue*)((const char*)s_GlobalMetadata + s_GlobalMetadataHeader->fieldDefaultValuesOffset);
    return defaultValues + index;
}

const uint8_t* il2cpp::vm::MetadataCache::GetFieldDefaultValueDataFromIndex(FieldIndex index)
{
    if (hybridclr::metadata::IsInterpreterIndex(index))
    {
        return hybridclr::metadata::MetadataModule::GetFieldOrParameterDefalutValue(index);
    }
    IL2CPP_ASSERT(index >= 0 && static_cast<uint32_t>(index) <= s_GlobalMetadataHeader->fieldAndParameterDefaultValueDataCount / sizeof(uint8_t));
    const uint8_t* defaultValuesData = (const uint8_t*)((const char*)s_GlobalMetadata + s_GlobalMetadataHeader->fieldAndParameterDefaultValueDataOffset);
    return defaultValuesData + index;
}

const Il2CppFieldDefaultValue* il2cpp::vm::MetadataCache::GetFieldDefaultValueForField(const FieldInfo* field)
{
    Il2CppClass* parent = field->parent;
    size_t fieldIndex = (field - parent->fields);
    if (il2cpp::vm::Type::IsGenericInstance(&parent->byval_arg))
        fieldIndex += il2cpp::vm::GenericClass::GetTypeDefinition(parent->generic_class)->typeDefinition->fieldStart;
    else
        fieldIndex += parent->typeDefinition->fieldStart;
    if (hybridclr::metadata::IsInterpreterType(field->parent))
    {
        return hybridclr::metadata::MetadataModule::GetFieldDefaultValueEntry((uint32_t)fieldIndex);
    }
    const Il2CppFieldDefaultValue *start = (const Il2CppFieldDefaultValue*)((const char*)s_GlobalMetadata + s_GlobalMetadataHeader->fieldDefaultValuesOffset);
    const Il2CppFieldDefaultValue *entry = start;
    while (entry < start + s_GlobalMetadataHeader->fieldDefaultValuesCount)
    {
        if (fieldIndex == entry->fieldIndex)
        {
            return entry;
        }
        entry++;
    }
    IL2CPP_ASSERT(0);
    return NULL;
}

const Il2CppParameterDefaultValue * il2cpp::vm::MetadataCache::GetParameterDefaultValueForParameter(const MethodInfo* method, const ParameterInfo* parameter)
{
    if (Method::IsGenericInstance(method))
        method = GetGenericMethodDefinition(method);

    IL2CPP_ASSERT(!Method::IsGenericInstance(method));

    if (method->methodDefinition == NULL)
        return NULL;

    ParameterIndex parameterIndex = method->methodDefinition->parameterStart + parameter->position;
    if (hybridclr::metadata::IsInterpreterMethod(method))
    {
        return hybridclr::metadata::MetadataModule::GetImage(method->klass)
            ->GetParameterDefaultValueEntryByRawIndex(parameterIndex);
    }
    const Il2CppParameterDefaultValue *start = (const Il2CppParameterDefaultValue*)((const char*)s_GlobalMetadata + s_GlobalMetadataHeader->parameterDefaultValuesOffset);
    const Il2CppParameterDefaultValue *entry = start;
    while (entry < start + s_GlobalMetadataHeader->parameterDefaultValuesCount)
    {
        if (parameterIndex == entry->parameterIndex)
            return entry;
        entry++;
    }

    return NULL;
}

const uint8_t* il2cpp::vm::MetadataCache::GetParameterDefaultValueDataFromIndex(ParameterIndex index)
{
    if (hybridclr::metadata::IsInterpreterIndex(index))
    {
        return hybridclr::metadata::MetadataModule::GetFieldOrParameterDefalutValue(index);
    }
    IL2CPP_ASSERT(index >= 0 && static_cast<uint32_t>(index) <= s_GlobalMetadataHeader->fieldAndParameterDefaultValueDataCount / sizeof(uint8_t));
    const uint8_t* defaultValuesData = (const uint8_t*)((const char*)s_GlobalMetadata + s_GlobalMetadataHeader->fieldAndParameterDefaultValueDataOffset);
    return defaultValuesData + index;
}

int il2cpp::vm::MetadataCache::GetFieldMarshaledSizeForField(const FieldInfo* field)
{
    Il2CppClass* parent = field->parent;
    size_t fieldIndex = (field - parent->fields);
    fieldIndex += parent->typeDefinition->fieldStart;
    const Il2CppFieldMarshaledSize *start = (const Il2CppFieldMarshaledSize*)((const char*)s_GlobalMetadata + s_GlobalMetadataHeader->fieldMarshaledSizesOffset);
    const Il2CppFieldMarshaledSize *entry = start;
    while ((intptr_t)entry < (intptr_t)start + s_GlobalMetadataHeader->fieldMarshaledSizesCount)
    {
        if (fieldIndex == entry->fieldIndex)
            return entry->size;
        entry++;
    }

    return -1;
}

const Il2CppMethodDefinition* il2cpp::vm::MetadataCache::GetMethodDefinitionFromIndex(MethodIndex index)
{
    if (hybridclr::metadata::IsInterpreterIndex(index))
    {
        return hybridclr::metadata::MetadataModule::GetMethodDefinitionFromIndex(index);
    }
    IL2CPP_ASSERT(index >= 0 && static_cast<uint32_t>(index) <= s_GlobalMetadataHeader->methodsCount / sizeof(Il2CppMethodDefinition));
    const Il2CppMethodDefinition* methods = (const Il2CppMethodDefinition*)((const char*)s_GlobalMetadata + s_GlobalMetadataHeader->methodsOffset);
    return methods + index;
}

const MethodInfo* il2cpp::vm::MetadataCache::GetMethodInfoFromMethodDefinitionIndex(MethodIndex index)
{
    if (hybridclr::metadata::IsInterpreterIndex(index))
    {
        return hybridclr::metadata::MetadataModule::GetMethodInfoFromMethodDefinitionIndex(index);
    }

    IL2CPP_ASSERT(index >= 0 && static_cast<uint32_t>(index) <= s_GlobalMetadataHeader->methodsCount / sizeof(Il2CppMethodDefinition));

    if (!s_MethodInfoDefinitionTable[index])
    {
        const Il2CppMethodDefinition* methodDefinition = GetMethodDefinitionFromIndex(index);
        Il2CppClass* typeInfo = GetTypeInfoFromTypeDefinitionIndex(methodDefinition->declaringType);
        il2cpp::vm::Class::SetupMethods(typeInfo);
        s_MethodInfoDefinitionTable[index] = typeInfo->methods[index - typeInfo->typeDefinition->methodStart];
    }

    return s_MethodInfoDefinitionTable[index];
}

const Il2CppPropertyDefinition* il2cpp::vm::MetadataCache::GetPropertyDefinitionFromIndex(PropertyIndex index)
{
    if (hybridclr::metadata::IsInterpreterIndex(index))
    {
        return hybridclr::metadata::MetadataModule::GetImageByEncodedIndex(index)->GetPropertyDefinitionFromIndex(hybridclr::metadata::DecodeMetadataIndex(index));
    }

    IL2CPP_ASSERT(index >= 0 && static_cast<uint32_t>(index) <= s_GlobalMetadataHeader->propertiesCount / sizeof(Il2CppPropertyDefinition));
    const Il2CppPropertyDefinition* properties = (const Il2CppPropertyDefinition*)((const char*)s_GlobalMetadata + s_GlobalMetadataHeader->propertiesOffset);
    return properties + index;
}

const Il2CppParameterDefinition* il2cpp::vm::MetadataCache::GetParameterDefinitionFromIndex(Il2CppClass* klass, ParameterIndex index)
{
    if (hybridclr::metadata::IsInterpreterType(klass))
    {
        return hybridclr::metadata::MetadataModule::GetParameterDefinitionFromIndex(klass->image, index);
    }

    IL2CPP_ASSERT(index >= 0 && static_cast<uint32_t>(index) <= s_GlobalMetadataHeader->parametersCount / sizeof(Il2CppParameterDefinition));
    const Il2CppParameterDefinition* parameters = (const Il2CppParameterDefinition*)((const char*)s_GlobalMetadata + s_GlobalMetadataHeader->parametersOffset);
    return parameters + index;
}

const Il2CppParameterDefinition* il2cpp::vm::MetadataCache::GetParameterDefinitionFromIndex(const Il2CppMethodDefinition* methodDef, ParameterIndex index)
{
    if (hybridclr::metadata::IsInterpreterIndex(methodDef->nameIndex))
    {
        return hybridclr::metadata::MetadataModule::GetParameterDefinitionFromIndex(hybridclr::metadata::MetadataModule::GetImage(methodDef)->GetIl2CppImage(), index);
    }
    IL2CPP_ASSERT(index >= 0 && static_cast<uint32_t>(index) <= s_GlobalMetadataHeader->parametersCount / sizeof(Il2CppParameterDefinition));
    const Il2CppParameterDefinition* parameters = (const Il2CppParameterDefinition*)((const char*)s_GlobalMetadata + s_GlobalMetadataHeader->parametersOffset);
    return parameters + index;
}

int32_t il2cpp::vm::MetadataCache::GetFieldOffsetFromIndexLocked(TypeIndex typeIndex, int32_t fieldIndexInType, FieldInfo* field, const il2cpp::os::FastAutoLock& lock)
{
    int32_t offset;
    if (hybridclr::metadata::IsInterpreterIndex(typeIndex))
    {
        offset = hybridclr::metadata::MetadataModule::GetImageByEncodedIndex(typeIndex)->GetFieldOffset(hybridclr::metadata::DecodeMetadataIndex(typeIndex), fieldIndexInType);
    }
    else
    {
        IL2CPP_ASSERT(typeIndex <= s_Il2CppMetadataRegistration->typeDefinitionsSizesCount);
        offset = s_Il2CppMetadataRegistration->fieldOffsets[typeIndex][fieldIndexInType];
    }
    if (offset < 0)
    {
        AddThreadLocalStaticOffsetForFieldLocked(field, offset & ~THREAD_LOCAL_STATIC_MASK, lock);
        return THREAD_STATIC_FIELD_OFFSET;
    }
    return offset;
}

void il2cpp::vm::MetadataCache::AddThreadLocalStaticOffsetForFieldLocked(FieldInfo* field, int32_t offset, const il2cpp::os::FastAutoLock& lock)
{
    s_ThreadLocalStaticOffsetMap.add(field, offset);
}

int32_t il2cpp::vm::MetadataCache::GetThreadLocalStaticOffsetForField(FieldInfo* field)
{
    IL2CPP_ASSERT(field->offset == THREAD_STATIC_FIELD_OFFSET);

    il2cpp::os::FastAutoLock lock(&g_MetadataLock);
    Il2CppThreadLocalStaticOffsetHashMapIter iter = s_ThreadLocalStaticOffsetMap.find(field);
    IL2CPP_ASSERT(iter != s_ThreadLocalStaticOffsetMap.end());
    return iter->second;
}

int32_t il2cpp::vm::MetadataCache::GetReferenceAssemblyIndexIntoAssemblyTable(int32_t referencedAssemblyTableIndex)
{
    IL2CPP_ASSERT(referencedAssemblyTableIndex >= 0 && static_cast<uint32_t>(referencedAssemblyTableIndex) <= s_GlobalMetadataHeader->referencedAssembliesCount / sizeof(int32_t));
    const int32_t* referenceAssemblyIndicies = (const int32_t*)((const char*)s_GlobalMetadata + s_GlobalMetadataHeader->referencedAssembliesOffset);
    return referenceAssemblyIndicies[referencedAssemblyTableIndex];
}

const TypeDefinitionIndex il2cpp::vm::MetadataCache::GetIndexForTypeDefinition(const Il2CppTypeDefinition* typeDefinition)
{
    IL2CPP_ASSERT(typeDefinition);
    if (hybridclr::metadata::IsInterpreterType(typeDefinition))
    {
        return static_cast<TypeDefinitionIndex>(hybridclr::metadata::MetadataModule::GetTypeEncodeIndex(typeDefinition));
    }
    const Il2CppTypeDefinition* typeDefinitions = (const Il2CppTypeDefinition*)((const char*)s_GlobalMetadata + s_GlobalMetadataHeader->typeDefinitionsOffset);

    IL2CPP_ASSERT(typeDefinition >= typeDefinitions && typeDefinition < typeDefinitions + s_GlobalMetadataHeader->typeDefinitionsCount);

    ptrdiff_t index = typeDefinition - typeDefinitions;
    IL2CPP_ASSERT(index <= std::numeric_limits<TypeDefinitionIndex>::max());
    return static_cast<TypeDefinitionIndex>(index);
}

const TypeDefinitionIndex il2cpp::vm::MetadataCache::GetIndexForTypeDefinition(const Il2CppClass* typeDefinition)
{
    return GetIndexForTypeDefinition(typeDefinition->typeDefinition);
}

const GenericParameterIndex il2cpp::vm::MetadataCache::GetIndexForGenericParameter(const Il2CppGenericParameter* genericParameter)
{
    const Il2CppGenericParameter* genericParameters = (const Il2CppGenericParameter*)((const char*)s_GlobalMetadata + s_GlobalMetadataHeader->genericParametersOffset);

    IL2CPP_ASSERT(genericParameter >= genericParameters && genericParameter < genericParameters + s_GlobalMetadataHeader->genericParametersCount);

    ptrdiff_t index = genericParameter - genericParameters;
    IL2CPP_ASSERT(index <= std::numeric_limits<GenericParameterIndex>::max());
    return static_cast<GenericParameterIndex>(index);
}

const MethodIndex il2cpp::vm::MetadataCache::GetIndexForMethodDefinition(const MethodInfo* method)
{
    IL2CPP_ASSERT(!method->is_inflated);
    const Il2CppMethodDefinition* methodDefinitions = (const Il2CppMethodDefinition*)((const char*)s_GlobalMetadata + s_GlobalMetadataHeader->methodsOffset);

    IL2CPP_ASSERT(method->methodDefinition >= methodDefinitions && method->methodDefinition < methodDefinitions + s_GlobalMetadataHeader->methodsCount);

    ptrdiff_t index = method->methodDefinition - methodDefinitions;
    IL2CPP_ASSERT(index <= std::numeric_limits<MethodIndex>::max());
    return static_cast<MethodIndex>(index);
}

static il2cpp::utils::OnceFlag s_CustomAttributesOnceFlag;

static void InitializeCustomAttributesCaches(void* arg)
{
    s_CustomAttributesCaches = (CustomAttributesCache**)IL2CPP_CALLOC(s_Il2CppCodeRegistration->customAttributeCount, sizeof(CustomAttributesCache*));
}

CustomAttributesCache* il2cpp::vm::MetadataCache::GenerateCustomAttributesCache(CustomAttributeIndex index)
{
    if (index == kCustomAttributeIndexInvalid)
        return NULL;

    if (hybridclr::metadata::IsInterpreterIndex(index))
    {
        return hybridclr::metadata::MetadataModule::GetImageByEncodedIndex(index)->GenerateCustomAttributesCacheInternal(hybridclr::metadata::DecodeMetadataIndex(index));
    }

    IL2CPP_ASSERT(index >= 0 && index < s_Il2CppCodeRegistration->customAttributeCount);
    IL2CPP_ASSERT(index >= 0 && index < static_cast<int32_t>(s_GlobalMetadataHeader->attributesInfoCount / sizeof(Il2CppCustomAttributeTypeRange)));

    il2cpp::utils::CallOnce(s_CustomAttributesOnceFlag, &InitializeCustomAttributesCaches, NULL);

    // use atomics rather than a Mutex here to avoid deadlock. The attribute generators call arbitrary managed code
    CustomAttributesCache* cache = il2cpp::os::Atomic::ReadPointer(&s_CustomAttributesCaches[index]);
    if (cache == NULL)
    {
        const Il2CppCustomAttributeTypeRange* attributeTypeRange = MetadataOffset<const Il2CppCustomAttributeTypeRange*>(s_GlobalMetadata, s_GlobalMetadataHeader->attributesInfoOffset, index);

        cache = (CustomAttributesCache*)IL2CPP_CALLOC(1, sizeof(CustomAttributesCache));
        cache->count = attributeTypeRange->count;
        cache->attributes = (Il2CppObject**)il2cpp::gc::GarbageCollector::AllocateFixed(sizeof(Il2CppObject *) * cache->count, 0);

        for (int32_t i = 0; i < attributeTypeRange->count; i++)
        {
            IL2CPP_ASSERT(attributeTypeRange->start + i < s_GlobalMetadataHeader->attributeTypesCount);
            TypeIndex typeIndex = *MetadataOffset<TypeIndex*>(s_GlobalMetadata, s_GlobalMetadataHeader->attributeTypesOffset, attributeTypeRange->start + i);
            cache->attributes[i] = il2cpp::vm::Object::New(GetTypeInfoFromTypeIndex(typeIndex));
            il2cpp::gc::GarbageCollector::SetWriteBarrier((void**)cache->attributes + i);
        }

        // generated code calls the attribute constructor and sets any fields/properties
        s_Il2CppCodeRegistration->customAttributeGenerators[index](cache);

        CustomAttributesCache* original = il2cpp::os::Atomic::CompareExchangePointer(&s_CustomAttributesCaches[index], cache, (CustomAttributesCache*)NULL);
        if (original)
        {
            // A non-NULL return value indicates some other thread already generated this cache.
            // We need to cleanup the resources we allocated
            il2cpp::gc::GarbageCollector::FreeFixed(cache->attributes);
            IL2CPP_FREE(cache);

            cache = original;
        }
    }

    return cache;
}

static int CompareTokens(const void* pkey, const void* pelem)
{
    return (int)(((Il2CppCustomAttributeTypeRange*)pkey)->token - ((Il2CppCustomAttributeTypeRange*)pelem)->token);
}

CustomAttributeIndex il2cpp::vm::MetadataCache::GetCustomAttributeIndex(const Il2CppImage* image, uint32_t token)
{
    if (hybridclr::metadata::IsInterpreterImage(image))
    {
        return hybridclr::metadata::MetadataModule::GetImage(image)->GetCustomAttributeIndex(token);
    }

    const Il2CppCustomAttributeTypeRange* attributeTypeRange = MetadataOffset<const Il2CppCustomAttributeTypeRange*>(s_GlobalMetadata, s_GlobalMetadataHeader->attributesInfoOffset, 0);

    Il2CppCustomAttributeTypeRange key;
    memset(&key, 0, sizeof(Il2CppCustomAttributeTypeRange));
    key.token = token;

    const Il2CppCustomAttributeTypeRange* res = (const Il2CppCustomAttributeTypeRange*)bsearch(&key, attributeTypeRange + image->customAttributeStart, image->customAttributeCount, sizeof(Il2CppCustomAttributeTypeRange), CompareTokens);

    if (res == NULL)
        return kCustomAttributeIndexInvalid;

    CustomAttributeIndex index = (CustomAttributeIndex)(res - attributeTypeRange);

    IL2CPP_ASSERT(index >= 0 && index < static_cast<int32_t>(s_GlobalMetadataHeader->attributesInfoCount / sizeof(Il2CppCustomAttributeTypeRange)));

    return index;
}

CustomAttributesCache* il2cpp::vm::MetadataCache::GenerateCustomAttributesCache(const Il2CppImage* image, uint32_t token)
{
    return GenerateCustomAttributesCache(GetCustomAttributeIndex(image, token));
}

bool il2cpp::vm::MetadataCache::HasAttribute(CustomAttributeIndex index, Il2CppClass* attribute)
{
    if (index == kCustomAttributeIndexInvalid)
        return false;

    IL2CPP_ASSERT(attribute);
    if (hybridclr::metadata::IsInterpreterIndex(index))
    {
        return hybridclr::metadata::MetadataModule::GetImageByEncodedIndex(index)->HasAttribute(hybridclr::metadata::DecodeMetadataIndex(index), attribute);
    }

    const Il2CppCustomAttributeTypeRange* attributeTypeRange = MetadataOffset<const Il2CppCustomAttributeTypeRange*>(s_GlobalMetadata, s_GlobalMetadataHeader->attributesInfoOffset, index);

    for (int32_t i = 0; i < attributeTypeRange->count; i++)
    {
        IL2CPP_ASSERT(attributeTypeRange->start + i < s_GlobalMetadataHeader->attributeTypesCount);
        TypeIndex typeIndex = *MetadataOffset<TypeIndex*>(s_GlobalMetadata, s_GlobalMetadataHeader->attributeTypesOffset, attributeTypeRange->start + i);
        Il2CppClass* klass = GetTypeInfoFromTypeIndex(typeIndex);

        if (il2cpp::vm::Class::HasParent(klass, attribute) || (il2cpp::vm::Class::IsInterface(attribute) && il2cpp::vm::Class::IsAssignableFrom(attribute, klass)))
            return true;
    }

    return false;
}

bool il2cpp::vm::MetadataCache::HasAttribute(const Il2CppImage* image, uint32_t token, Il2CppClass* attribute)
{
    return HasAttribute(GetCustomAttributeIndex(image, token), attribute);
}

Il2CppString* il2cpp::vm::MetadataCache::GetStringLiteralFromIndex(StringLiteralIndex index)
{
    if (index == kStringLiteralIndexInvalid)
        return NULL;

    IL2CPP_ASSERT(index >= 0 && static_cast<uint32_t>(index) < s_GlobalMetadataHeader->stringLiteralCount / sizeof(Il2CppStringLiteral) && "Invalid string literal index ");

    if (s_StringLiteralTable[index])
        return s_StringLiteralTable[index];

    const Il2CppStringLiteral* stringLiteral = (const Il2CppStringLiteral*)((const char*)s_GlobalMetadata + s_GlobalMetadataHeader->stringLiteralOffset) + index;
    Il2CppString* newString = String::NewLen((const char*)s_GlobalMetadata + s_GlobalMetadataHeader->stringLiteralDataOffset + stringLiteral->dataIndex, stringLiteral->length);
    Il2CppString* prevString = il2cpp::os::Atomic::CompareExchangePointer<Il2CppString>(s_StringLiteralTable + index, newString, NULL);
    if (prevString == NULL)
    {
        il2cpp::gc::GarbageCollector::SetWriteBarrier((void**)s_StringLiteralTable + index);
        return newString;
    }
    return prevString;
}

const char* il2cpp::vm::MetadataCache::GetStringFromIndex(StringIndex index)
{
    if (hybridclr::metadata::IsInterpreterIndex(index))
    {
        return hybridclr::metadata::MetadataModule::GetStringFromEncodeIndex(index);
    }
    IL2CPP_ASSERT(index <= s_GlobalMetadataHeader->stringCount);
    const char* strings = ((const char*)s_GlobalMetadata + s_GlobalMetadataHeader->stringOffset) + index;
    return strings;
}

FieldInfo* il2cpp::vm::MetadataCache::GetFieldInfoFromIndex(EncodedMethodIndex index)
{
    IL2CPP_ASSERT(s_GlobalMetadataHeader->fieldRefsCount >= 0 && index <= static_cast<uint32_t>(s_GlobalMetadataHeader->fieldRefsCount));

    const Il2CppFieldRef* fieldRef = MetadataOffset<const Il2CppFieldRef*>(s_GlobalMetadata, s_GlobalMetadataHeader->fieldRefsOffset, index);
    Il2CppClass* typeInfo = GetTypeInfoFromTypeIndex(fieldRef->typeIndex);
    return typeInfo->fields + fieldRef->fieldIndex;
}

static bool IsMatchingUsage(Il2CppMetadataUsage usage, const il2cpp::utils::dynamic_array<Il2CppMetadataUsage>& expectedUsages)
{
    if (expectedUsages.empty())
        return true;

    size_t numberOfExpectedUsages = expectedUsages.size();
    for (size_t i = 0; i < numberOfExpectedUsages; ++i)
    {
        if (expectedUsages[i] == usage)
            return true;
    }

    return false;
}

// This method can be called from multiple threads, so it does have a data race. However, each
// thread is reading from the same read-only metadata, so each thread will set the same values.
// Therefore, we can safely ignore thread sanitizer issues in this method.
void il2cpp::vm::MetadataCache::IntializeMethodMetadataRange(uint32_t start, uint32_t count, const il2cpp::utils::dynamic_array<Il2CppMetadataUsage>& expectedUsages, bool throwOnError) IL2CPP_DISABLE_TSAN
{
    for (uint32_t i = 0; i < count; i++)
    {
        uint32_t offset = start + i;
        IL2CPP_ASSERT(s_GlobalMetadataHeader->metadataUsagePairsCount >= 0 && offset <= static_cast<uint32_t>(s_GlobalMetadataHeader->metadataUsagePairsCount));
        const Il2CppMetadataUsagePair* metadataUsagePairs = MetadataOffset<const Il2CppMetadataUsagePair*>(s_GlobalMetadata, s_GlobalMetadataHeader->metadataUsagePairsOffset, offset);
        uint32_t destinationIndex = metadataUsagePairs->destinationIndex;
        uint32_t encodedSourceIndex = metadataUsagePairs->encodedSourceIndex;

        Il2CppMetadataUsage usage = GetEncodedIndexType(encodedSourceIndex);
        if (IsMatchingUsage(usage, expectedUsages))
        {
            uint32_t decodedIndex = GetDecodedMethodIndex(encodedSourceIndex);
            switch (usage)
            {
                case kIl2CppMetadataUsageTypeInfo:
                    *s_Il2CppMetadataRegistration->metadataUsages[destinationIndex] = GetTypeInfoFromTypeIndex(decodedIndex, throwOnError);
                    break;
                case kIl2CppMetadataUsageIl2CppType:
                    *s_Il2CppMetadataRegistration->metadataUsages[destinationIndex] = const_cast<Il2CppType*>(GetIl2CppTypeFromIndex(decodedIndex));
                    break;
                case kIl2CppMetadataUsageMethodDef:
                case kIl2CppMetadataUsageMethodRef:
                    *s_Il2CppMetadataRegistration->metadataUsages[destinationIndex] = const_cast<MethodInfo*>(GetMethodInfoFromIndex(encodedSourceIndex));
                    break;
                case kIl2CppMetadataUsageFieldInfo:
                    *s_Il2CppMetadataRegistration->metadataUsages[destinationIndex] = GetFieldInfoFromIndex(decodedIndex);
                    break;
                case kIl2CppMetadataUsageStringLiteral:
                    *s_Il2CppMetadataRegistration->metadataUsages[destinationIndex] = GetStringLiteralFromIndex(decodedIndex);
                    break;
                default:
                    IL2CPP_NOT_IMPLEMENTED(il2cpp::vm::MetadataCache::InitializeMethodMetadata);
                    break;
            }
        }
    }
}

void il2cpp::vm::MetadataCache::InitializeAllMethodMetadata()
{
    il2cpp::utils::dynamic_array<Il2CppMetadataUsage> onlyAcceptMethodUsages;
    onlyAcceptMethodUsages.push_back(kIl2CppMetadataUsageMethodDef);
    onlyAcceptMethodUsages.push_back(kIl2CppMetadataUsageMethodRef);
    onlyAcceptMethodUsages.push_back(kIl2CppMetadataUsageTypeInfo);
    IntializeMethodMetadataRange(0, s_GlobalMetadataHeader->metadataUsagePairsCount / sizeof(Il2CppMetadataUsagePair), onlyAcceptMethodUsages, false);
}

void il2cpp::vm::MetadataCache::InitializeMethodMetadata(uint32_t index)
{
    IL2CPP_ASSERT(s_GlobalMetadataHeader->metadataUsageListsCount >= 0 && index <= static_cast<uint32_t>(s_GlobalMetadataHeader->metadataUsageListsCount));

    const Il2CppMetadataUsageList* metadataUsageLists = MetadataOffset<const Il2CppMetadataUsageList*>(s_GlobalMetadata, s_GlobalMetadataHeader->metadataUsageListsOffset, index);

    uint32_t start = metadataUsageLists->start;
    uint32_t count = metadataUsageLists->count;

    il2cpp::utils::dynamic_array<Il2CppMetadataUsage> acceptAllUsages;
    IntializeMethodMetadataRange(start, count, acceptAllUsages, true);
}

void il2cpp::vm::MetadataCache::WalkPointerTypes(WalkTypesCallback callback, void* context)
{
    il2cpp::os::FastAutoLock lock(&s_MetadataCache.m_CacheMutex);
    for (PointerTypeMap::iterator it = s_MetadataCache.m_PointerTypes.begin(); it != s_MetadataCache.m_PointerTypes.end(); it++)
    {
        callback(it->second, context);
    }
}

const Il2CppTypeDefinition* il2cpp::vm::MetadataCache::GetTypeHandleFromIndex(TypeDefinitionIndex typeIndex)
{
    return GetTypeDefinitionFromIndex(typeIndex);
}

const Il2CppTypeDefinition* il2cpp::vm::MetadataCache::GetTypeDefinitionFromIl2CppType(const Il2CppType* type)
{
    IL2CPP_ASSERT(type->type == IL2CPP_TYPE_VALUETYPE || type->type == IL2CPP_TYPE_CLASS);
    return type->data.typeHandle;
}

const Il2CppType* il2cpp::vm::MetadataCache::GetIl2CppTypeFromClass(const Il2CppClass* klass)
{
    return &klass->byval_arg;
}

Il2CppClass* il2cpp::vm::MetadataCache::GetIl2CppClassFromTypeDefinition(const Il2CppTypeDefinition* typeDefinition)
{
    return GetTypeInfoFromTypeDefinitionIndex(GetTypeDefinitionIndexFromTypeDefinition(typeDefinition));
}


const TypeDefinitionIndex il2cpp::vm::MetadataCache::GetTypeDefinitionIndexFromTypeDefinition(const Il2CppTypeDefinition* typeDefinition)
{
    if (hybridclr::metadata::IsInterpreterType(typeDefinition))
    {
        return hybridclr::metadata::MetadataModule::GetTypeEncodeIndex(typeDefinition);
    }
    const Il2CppTypeDefinition* typeDefinitions = (const Il2CppTypeDefinition*)((const char*)s_GlobalMetadata + s_GlobalMetadataHeader->typeDefinitionsOffset);

    IL2CPP_ASSERT(typeDefinition >= typeDefinitions && typeDefinition < typeDefinitions + s_GlobalMetadataHeader->typeDefinitionsCount);

    ptrdiff_t index = typeDefinition - typeDefinitions;
    IL2CPP_ASSERT(index <= std::numeric_limits<TypeDefinitionIndex>::max());
    return static_cast<TypeDefinitionIndex>(index);
}

const Il2CppTypeDefinition* il2cpp::vm::MetadataCache::GetAssemblyTypeHandle(const Il2CppImage* image, int32_t index)
{
    if (hybridclr::metadata::IsInterpreterImage(image))
    {
        return hybridclr::metadata::MetadataModule::GetAssemblyTypeHandleFromRawIndex(image, index);
    }
    TypeDefinitionIndex typeDefinitionIndex = image->typeStart + index;
    IL2CPP_ASSERT(typeDefinitionIndex < s_GlobalMetadataHeader->typeDefinitionsCount);
    const Il2CppTypeDefinition* typeDefinitions = (const Il2CppTypeDefinition*)((const char*)s_GlobalMetadata + s_GlobalMetadataHeader->typeDefinitionsOffset);
    return typeDefinitions + typeDefinitionIndex;
}

Il2CppMetadataTypeHandle il2cpp::vm::MetadataCache::GetNestedTypes(Il2CppMetadataTypeHandle handle, void** iter)
{
    if (!iter)
        return NULL;

    const Il2CppTypeDefinition* typeDefinition = reinterpret_cast<const Il2CppTypeDefinition*>(handle);
    if (hybridclr::metadata::IsInterpreterType(typeDefinition))
    {
        return hybridclr::metadata::MetadataModule::GetNestedTypes(handle, iter);
    }

    const TypeDefinitionIndex* nestedTypeIndices = (const TypeDefinitionIndex*)((const char*)s_GlobalMetadata + s_GlobalMetadataHeader->nestedTypesOffset);

    if (!*iter)
    {
        if (typeDefinition->nested_type_count == 0)
            return NULL;

        *iter = (void*)(nestedTypeIndices + typeDefinition->nestedTypesStart);
        return GetTypeHandleFromIndex(nestedTypeIndices[typeDefinition->nestedTypesStart]);
    }

    TypeDefinitionIndex* nestedTypeAddress = (TypeDefinitionIndex*)*iter;
    nestedTypeAddress++;
    ptrdiff_t index = nestedTypeAddress - nestedTypeIndices;

    if (index < typeDefinition->nestedTypesStart + typeDefinition->nested_type_count)
    {
        *iter = nestedTypeAddress;
        return GetTypeHandleFromIndex(*nestedTypeAddress);
    }

    return NULL;
}

il2cpp::vm::PackingSize il2cpp::vm::MetadataCache::ConvertPackingSizeToEnum(uint8_t packingSize)
{
    switch (packingSize)
    {
    case 0:
        return PackingSize::Zero;
    case 1:
        return PackingSize::One;
    case 2:
        return PackingSize::Two;
    case 4:
        return PackingSize::Four;
    case 8:
        return PackingSize::Eight;
    case 16:
        return PackingSize::Sixteen;
    case 32:
        return PackingSize::ThirtyTwo;
    case 64:
        return PackingSize::SixtyFour;
    case 128:
        return OneHundredTwentyEight;
    default:
        Assert(0 && "Invalid packing size!");
        return PackingSize::Zero;
    }
}


const Il2CppType* il2cpp::vm::MetadataCache::GetInterfaceFromOffset(const Il2CppTypeDefinition* typeDefinition, InterfacesIndex offset)
{
    if (hybridclr::metadata::IsInterpreterType(typeDefinition))
    {
        return hybridclr::metadata::MetadataModule::GetImage(typeDefinition)->GetInterfaceFromOffset(typeDefinition, offset);
    }

    InterfacesIndex index = typeDefinition->interfacesStart + offset;
    IL2CPP_ASSERT(index >= 0 && static_cast<uint32_t>(index) <= s_GlobalMetadataHeader->interfacesCount / sizeof(TypeIndex));
    const TypeIndex* interfaceIndices = (const TypeIndex*)((const char*)s_GlobalMetadata + s_GlobalMetadataHeader->interfacesOffset);

    return GetIl2CppTypeFromIndex(interfaceIndices[index]);
}

Il2CppMetadataGenericContainerHandle il2cpp::vm::MetadataCache::GetGenericContainerFromGenericClass(const Il2CppGenericClass* genericClass)
{
    const Il2CppTypeDefinition* genericType = reinterpret_cast<const Il2CppTypeDefinition*>(GetTypeDefinitionFromIl2CppType(genericClass->type));
    return GetGenericContainerFromIndex(genericType->genericContainerIndex);
}

const Il2CppMethodDefinition* il2cpp::vm::MetadataCache::GetMethodDefinitionFromVTableSlot(const Il2CppTypeDefinition* typeDefinition, int32_t vTableSlot)
{
    if (hybridclr::metadata::IsInterpreterType(typeDefinition))
    {
        return hybridclr::metadata::MetadataModule::GetMethodDefinitionFromVTableSlot(typeDefinition, vTableSlot);
    }

    //const Il2CppTypeDefinition* typeDefinition = reinterpret_cast<const Il2CppTypeDefinition*>(klass->typeMetadataHandle);

    uint32_t index = typeDefinition->vtableStart + vTableSlot;
    IL2CPP_ASSERT(index >= 0 && index <= s_GlobalMetadataHeader->vtableMethodsCount / sizeof(EncodedMethodIndex));
    const EncodedMethodIndex* vTableMethodReferences = (const EncodedMethodIndex*)((const char*)s_GlobalMetadata + s_GlobalMetadataHeader->vtableMethodsOffset);
    EncodedMethodIndex vTableMethodReference = vTableMethodReferences[index];

    if (vTableMethodReference == 0)
    {
        return nullptr;
    }

    IL2CPP_ASSERT(vTableMethodReference != 0);

    return GetMethodInfoFromIndex(vTableMethodReference)->methodDefinition;
}
