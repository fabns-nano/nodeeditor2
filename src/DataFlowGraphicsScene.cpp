#include "DataFlowGraphicsScene.hpp"

#include "ConnectionGraphicsObject.hpp"
#include "GraphicsView.hpp"
#include "NodeDelegateModelRegistry.hpp"
#include "NodeGraphicsObject.hpp"
#include "UndoCommands.hpp"

#include <QClipboard>
#include <QtWidgets/QApplication>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QGraphicsSceneMoveEvent>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QTreeWidget>
#include <QtWidgets/QWidgetAction>

#include <QtCore/QBuffer>
#include <QtCore/QByteArray>
#include <QtCore/QDebug>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonParseError>
#include <QtCore/QJsonValue>
#include <QtCore/QMimeData>
#include <QtCore/QtGlobal>

#include <stdexcept>
#include <utility>
#include <vector>

namespace QtNodes {

DataFlowGraphicsScene::DataFlowGraphicsScene(DataFlowGraphModel &graphModel, QObject *parent)
    : BasicGraphicsScene(graphModel, parent)
    , _graphModel(graphModel)
{
    connect(&_graphModel,
            &DataFlowGraphModel::inPortDataWasSet,
            [this](NodeId const nodeId, PortType const, PortIndex const) { onNodeUpdated(nodeId); });
}

// TODO constructor for an empyt scene?

std::vector<NodeId> DataFlowGraphicsScene::selectedNodes() const
{
    QList<QGraphicsItem *> graphicsItems = selectedItems();

    std::vector<NodeId> result;
    result.reserve(graphicsItems.size());

    for (QGraphicsItem *item : graphicsItems) {
        auto ngo = qgraphicsitem_cast<NodeGraphicsObject *>(item);

        if (ngo != nullptr) {
            result.push_back(ngo->nodeId());
        }
    }

    return result;
}

QMenu *DataFlowGraphicsScene::createSceneMenu(QPointF const scenePos)
{
    QMenu *modelMenu = new QMenu();

    auto *txtBox = new QLineEdit(modelMenu);
    txtBox->setPlaceholderText(QStringLiteral("Filter"));
    txtBox->setClearButtonEnabled(true);
    auto *txtBoxAction = new QWidgetAction(modelMenu);
    txtBoxAction->setDefaultWidget(txtBox);
    modelMenu->addAction(txtBoxAction);

    QTreeWidget *treeView = new QTreeWidget(modelMenu);
    treeView->header()->close();
    auto *treeViewAction = new QWidgetAction(modelMenu);
    treeViewAction->setDefaultWidget(treeView);
    modelMenu->addAction(treeViewAction);

    auto registry = _graphModel.dataModelRegistry();

    for (auto const &cat : registry->categories()) {
        auto item = new QTreeWidgetItem(treeView);
        item->setText(0, cat);
        item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
    }

    for (auto const &assoc : registry->registeredModelsCategoryAssociation()) {
        QList<QTreeWidgetItem *> parent = treeView->findItems(assoc.second, Qt::MatchExactly);
        if (parent.count() <= 0)
            continue;
        auto item = new QTreeWidgetItem(parent.first());
        item->setText(0, assoc.first);
    }

    treeView->expandAll();

    connect(treeView,
            &QTreeWidget::itemClicked,
            [this, modelMenu, scenePos](QTreeWidgetItem *item, int) {
                if (!(item->flags() & Qt::ItemIsSelectable)) {
                    return;
                }
                this->undoStack().push(new CreateCommand(this, item->text(0), scenePos));
                modelMenu->close();
            });

    connect(txtBox, &QLineEdit::textChanged, [treeView](const QString &text) {
        QTreeWidgetItemIterator categoryIt(treeView, QTreeWidgetItemIterator::HasChildren);
        while (*categoryIt)
            (*categoryIt++)->setHidden(true);

        QTreeWidgetItemIterator it(treeView, QTreeWidgetItemIterator::NoChildren);
        while (*it) {
            auto modelName = (*it)->text(0);
            const bool match = modelName.contains(text, Qt::CaseInsensitive);
            (*it)->setHidden(!match);
            if (match) {
                QTreeWidgetItem *parent = (*it)->parent();
                while (parent) {
                    parent->setHidden(false);
                    parent = parent->parent();
                }
            }
            ++it;
        }
    });

    txtBox->setFocus();

    // Separator to action menu
    modelMenu->addSeparator();

    QAction *loadGroupAction = modelMenu->addAction("Load Group...");
    QObject::connect(loadGroupAction, &QAction::triggered, this, &BasicGraphicsScene::loadGroupFile);

    QAction *copyAction = modelMenu->addAction("Copy");
    copyAction->setShortcut(QKeySequence::Copy);

    QAction *cutAction = modelMenu->addAction("Cut");
    cutAction->setShortcut(QKeySequence::Cut);

    QAction *pasteAction = modelMenu->addAction("Paste");
    pasteAction->setShortcut(QKeySequence::Paste);

    connect(copyAction, &QAction::triggered, this, &BasicGraphicsScene::onCopySelectedObjects);
    connect(cutAction, &QAction::triggered, [this] {
        onCopySelectedObjects();
        onDeleteSelectedObjects();
    });
    connect(pasteAction, &QAction::triggered, [this, scenePos] {
        onPasteSelectedObjects(scenePos);
    });

    bool hasSelection = !selectedItems().isEmpty();
    copyAction->setEnabled(hasSelection);
    cutAction->setEnabled(hasSelection);

    auto hasPasteableClipboardData = [] {
        QClipboard const *clipboard = QApplication::clipboard();
        if (clipboard == nullptr)
            return false;

        QMimeData const *mimeData = clipboard->mimeData();
        if (mimeData == nullptr)
            return false;

        QJsonDocument json;
        if (mimeData->hasFormat("application/qt-nodes-graph")) {
            json = QJsonDocument::fromJson(mimeData->data("application/qt-nodes-graph"));
        } else if (mimeData->hasText()) {
            json = QJsonDocument::fromJson(mimeData->text().toUtf8());
        } else {
            return false;
        }

        if (!json.isObject())
            return false;

        return !json.object()["nodes"].toArray().empty();
    };

    pasteAction->setEnabled(hasPasteableClipboardData());

    modelMenu->setAttribute(Qt::WA_DeleteOnClose);

    return modelMenu;
}

bool DataFlowGraphicsScene::save() const
{
    QString fileName = QFileDialog::getSaveFileName(nullptr,
                                                    tr("Open Flow Scene"),
                                                    QDir::homePath(),
                                                    tr("Flow Scene Files (*.flow)"));

    if (!fileName.isEmpty()) {
        if (!fileName.endsWith("flow", Qt::CaseInsensitive))
            fileName += ".flow";

        QFile file(fileName);
        if (file.open(QIODevice::WriteOnly)) {
            std::vector<DataFlowGraphModel::GroupData> groupsData;
            groupsData.reserve(groups().size());
            for (auto const &[groupId, groupPtr] : groups()) {
                if (!groupPtr)
                    continue;

                DataFlowGraphModel::GroupData groupData;
                groupData.id = groupId;
                groupData.name = groupPtr->name();
                groupData.nodeIds = groupPtr->nodeIDs();
                groupData.locked = groupPtr->groupGraphicsObject().locked();

                groupsData.push_back(std::move(groupData));
            }

            _graphModel.setGroups(std::move(groupsData));

            QJsonObject sceneJson = _graphModel.save();

            file.write(QJsonDocument(sceneJson).toJson());
            return true;
        }
    }
    return false;
}

bool DataFlowGraphicsScene::load()
{
    QString fileName = QFileDialog::getOpenFileName(nullptr,
                                                    tr("Open Flow Scene"),
                                                    QDir::homePath(),
                                                    tr("Flow Scene Files (*.flow)"));

    if (!QFileInfo::exists(fileName))
        return false;

    QFile file(fileName);

    if (!file.open(QIODevice::ReadOnly))
        return false;

    clearScene();

    QByteArray const wholeFile = file.readAll();

    QJsonParseError parseError{};
    QJsonDocument const sceneDocument = QJsonDocument::fromJson(wholeFile, &parseError);
    if (parseError.error != QJsonParseError::NoError || !sceneDocument.isObject())
        return false;

    QJsonObject const sceneJson = sceneDocument.object();

    _graphModel.load(sceneJson);

    for (DataFlowGraphModel::GroupData const &groupData : _graphModel.groups()) {
        std::vector<NodeGraphicsObject *> groupNodes;
        groupNodes.reserve(groupData.nodeIds.size());

        for (NodeId const nodeId : groupData.nodeIds) {
            if (auto *nodeObject = nodeGraphicsObject(nodeId)) {
                groupNodes.push_back(nodeObject);
            }
        }

        if (groupNodes.empty())
            continue;

        auto const groupWeak = createGroup(groupNodes, groupData.name, groupData.id);
        if (auto group = groupWeak.lock()) {
            group->groupGraphicsObject().lock(groupData.locked);
        }
    }

    Q_EMIT sceneLoaded();

    return true;
}

void DataFlowGraphicsScene::updateConnectionGraphics(
    const std::unordered_set<ConnectionId> &connections, bool state)
{
    for (auto const &c : connections) {
        if (auto *cgo = connectionGraphicsObject(c)) {
            cgo->connectionState().setFrozen(state);
            cgo->update();
        }
    }
}

} // namespace QtNodes
