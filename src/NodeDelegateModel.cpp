#include "NodeDelegateModel.hpp"

#include "StyleCollection.hpp"

namespace QtNodes {

NodeDelegateModel::NodeDelegateModel()
    : _nodeStyle(StyleCollection::nodeStyle())
{
    // Derived classes can initialize specific style here
}

QJsonObject NodeDelegateModel::save() const
{
    QJsonObject modelJson;

    modelJson["model-name"] = name();

    return modelJson;
}

void NodeDelegateModel::load(QJsonObject const &)
{
    //
}

void NodeDelegateModel::setValidationState(const NodeValidationState &validationState)
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

QIcon NodeDelegateModel::processingStatusIcon() const
{
    switch (_processingStatus) {
    case NodeProcessingStatus::NoStatus:
        return {};
    case NodeProcessingStatus::Updated:
        return _nodeStyle.statusUpdated;
    case NodeProcessingStatus::Processing:
        return _nodeStyle.statusProcessing;
    case NodeProcessingStatus::Pending:
        return _nodeStyle.statusPending;
    case NodeProcessingStatus::Empty:
        return _nodeStyle.statusEmpty;
    case NodeProcessingStatus::Failed:
        return _nodeStyle.statusInvalid;
    case NodeProcessingStatus::Partial:
        return _nodeStyle.statusPartial;
    }

    return {};
}

void NodeDelegateModel::setNodeProcessingStatus(NodeProcessingStatus status)
{
    _processingStatus = status;
}

} // namespace QtNodes
