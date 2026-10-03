module;
#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_IMPLEMENTATION
#include <tiny_gltf.h>
export module GPP.Graphics:Assets.Gltf;

export namespace tinygltf
{
    using tinygltf::TinyGLTF;
    using tinygltf::Model;
    using tinygltf::Scene;
    using tinygltf::Node;
    using tinygltf::Mesh;
    using tinygltf::Primitive;
    using tinygltf::Accessor;
    using tinygltf::BufferView;
    using tinygltf::Buffer;
    using tinygltf::Material;
    using tinygltf::Texture;
    using tinygltf::Image;
    using tinygltf::Sampler;
    using tinygltf::Camera;
    using tinygltf::Animation;
    using tinygltf::Skin;
    using tinygltf::Value;
}
