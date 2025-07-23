#include "NodeDelegateModel.hpp"

#include "StyleCollection.hpp"

namespace QtNodes {

NodeDelegateModel::NodeDelegateModel()
    : _nodeStyle(StyleCollection::nodeStyle())
    , _labelVisible(true)
{
    // Derived classes can initialize specific style here
}

QJsonObject NodeDelegateModel::save() const
{
    QJsonObject modelJson;

    modelJson["model-name"] = name();
    modelJson["label"] = _label;
    modelJson["label-visible"] = _labelVisible;

    return modelJson;
}

void NodeDelegateModel::load(QJsonObject const &json)
{
    if (json.contains("label"))
        _label = json["label"].toString();
    if (json.contains("label-visible"))
        _labelVisible = json["label-visible"].toBool();
}

void NodeDelegateModel::setValidatonState(const NodeValidationState &validationState)
{
    _nodeValidationState = validationState;
}

ConnectionPolicy NodeDelegateModel::portConnectionPolicy(PortType portType, PortIndex) const
{
    auto result = ConnectionPolicy::One;
    switch (portType) {
    case PortType::In:
        result = ConnectionPolicy::One;
        break;
    case PortType::Out:
        result = ConnectionPolicy::Many;
        break;
    case PortType::None:
        break;
    }

    return result;
}

NodeStyle const &NodeDelegateModel::nodeStyle() const
{
    return _nodeStyle;
}

void NodeDelegateModel::setNodeStyle(NodeStyle const &style)
{
    _nodeStyle = style;
}

} // namespace QtNodes
