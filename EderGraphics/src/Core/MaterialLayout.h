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

    const std::vector<MaterialFieldInfo>& GetFields()    const { return fields; }
    size_t                                GetBlockSize() const;
    const MaterialFieldInfo*              Find(const std::string& name) const;

private:
    MaterialLayout& Add(const std::string& name, MaterialPropertyType type);

    std::vector<MaterialFieldInfo> fields;
    size_t                         rawSize = 0;
};
