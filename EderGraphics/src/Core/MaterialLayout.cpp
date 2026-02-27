#include "MaterialLayout.h"
#include <algorithm>
#include <stdexcept>

MaterialLayout& MaterialLayout::Add(const std::string& name, MaterialPropertyType type)
{
    for (const auto& f : fields)
        if (f.name == name)
            throw std::runtime_error("MaterialLayout: duplicate field '" + name + "'");

    size_t align  = MaterialPropertyAlignment(type);
    size_t offset = (rawSize + align - 1) & ~(align - 1);
    size_t size   = MaterialPropertySize(type);

    fields.push_back({ name, type, offset, size });
    rawSize = offset + size;
    return *this;
}

MaterialLayout& MaterialLayout::AddFloat(const std::string& name) { return Add(name, MaterialPropertyType::Float); }
MaterialLayout& MaterialLayout::AddInt  (const std::string& name) { return Add(name, MaterialPropertyType::Int);   }
MaterialLayout& MaterialLayout::AddVec2 (const std::string& name) { return Add(name, MaterialPropertyType::Vec2);  }
MaterialLayout& MaterialLayout::AddVec3 (const std::string& name) { return Add(name, MaterialPropertyType::Vec3);  }
MaterialLayout& MaterialLayout::AddVec4 (const std::string& name) { return Add(name, MaterialPropertyType::Vec4);  }
MaterialLayout& MaterialLayout::AddMat4 (const std::string& name) { return Add(name, MaterialPropertyType::Mat4);  }

size_t MaterialLayout::GetBlockSize() const
{
    if (rawSize == 0) return 0;
    return (rawSize + 15) & ~size_t(15);
}

const MaterialFieldInfo* MaterialLayout::Find(const std::string& name) const
{
    auto it = std::find_if(fields.begin(), fields.end(),
        [&](const MaterialFieldInfo& f) { return f.name == name; });
    return it != fields.end() ? &(*it) : nullptr;
}
