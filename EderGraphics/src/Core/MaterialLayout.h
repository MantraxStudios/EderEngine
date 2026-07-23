#pragma once
#include "MaterialProperty.h"
#include <vector>
#include "DLLHeader.h"

struct MaterialFieldInfo
{
    std::string          name;
    MaterialPropertyType type;
    size_t               offset;
    size_t               size;
};

class EDERGRAPHICS_API MaterialLayout
{
public:
    MaterialLayout() = default;

    MaterialLayout& AddFloat(const std::string& name);
    MaterialLayout& AddInt  (const std::string& name);
    MaterialLayout& AddVec2 (const std::string& name);
    MaterialLayout& AddVec3 (const std::string& name);
    MaterialLayout& AddVec4 (const std::string& name);
    MaterialLayout& AddMat4 (const std::string& name);

    // The standard PBR material block used by triangle.frag. Centralised here so
    // every material creation site stays byte-compatible with the shader UBO.
    //   vec4 albedo; float roughness, metallic, emissiveIntensity, alphaThreshold;
    //   float hasNormalMap, hasRoughMap, hasEmissiveMap;
    static MaterialLayout Standard();

    const std::vector<MaterialFieldInfo>& GetFields()    const { return fields; }
    size_t                                GetBlockSize() const;
    const MaterialFieldInfo*              Find(const std::string& name) const;

private:
    MaterialLayout& Add(const std::string& name, MaterialPropertyType type);

    std::vector<MaterialFieldInfo> fields;
    size_t                         rawSize = 0;
};
