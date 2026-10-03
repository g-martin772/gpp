module;
#include <yaml-cpp/yaml.h>
export module GPP.Core:IO.Yaml;

export namespace YAML
{
    using YAML::Node;
    using YAML::Load;
    using YAML::LoadFile;
    using YAML::LoadAll;
    using YAML::Emitter;
    using YAML::EMITTER_MANIP;
    using YAML::BeginSeq;
    using YAML::EndSeq;
    using YAML::BeginMap;
    using YAML::EndMap;
    using YAML::Key;
    using YAML::Value;
    using YAML::Newline;
    using YAML::Comment;
    using YAML::Flow;
    using YAML::Block;
    using YAML::ParserException;
    using YAML::BadConversion;
    using YAML::Exception;

    namespace NodeType
    {
        using YAML::NodeType::value;
        using YAML::NodeType::Undefined;
        using YAML::NodeType::Null;
        using YAML::NodeType::Scalar;
        using YAML::NodeType::Sequence;
        using YAML::NodeType::Map;
    }
}
