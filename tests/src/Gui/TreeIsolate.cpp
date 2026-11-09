// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QAction>
#include <QTest>

#include <Inventor/SoDB.h>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObjectGroup.h>
#include <App/FeaturePython.h>
#include <App/Link.h>
#include <App/Part.h>
#include <Base/Interpreter.h>
#include <src/App/InitApplication.h>

#include "Gui/Application.h"
#include "Gui/Document.h"
#include "Gui/MetaTypes.h"
#include "Gui/SoFCDB.h"
#include "Gui/Tree.h"
#include "Gui/ViewProviderDocumentObject.h"

class TreeIsolateTest: public QObject
{
    Q_OBJECT

public:
    TreeIsolateTest()
    {
        tests::initApplication();
        Gui::Application::initApplication();
        Gui::Application::initOpenInventor();
        if (!Gui::Application::Instance) {
            new Gui::Application(true);
        }
    }

private Q_SLOTS:
    void init()
    {
        tree = std::make_unique<Gui::TreeWidget>("TestTree");
        docName = App::GetApplication().getUniqueDocumentName("test_isolate");
        App::DocumentInitFlags flags;
        flags.createView = false;
        doc = App::GetApplication().newDocument(docName.c_str(), "testUser", flags);
        guiDoc = Gui::Application::Instance->getDocument(doc);
        QVERIFY(guiDoc != nullptr);
        docItem = tree->getDocumentItem(guiDoc);
        QVERIFY(docItem != nullptr);
    }

    void cleanup()
    {
        Base::PyGILStateLocker lock;
        App::GetApplication().closeDocument(docName.c_str());
        tree.reset();
    }

    void isolateLeafObject_HidesOthersAndRestoresOnUnisolate()
    {
        auto* objA = doc->addObject("App::FeaturePython", "ObjA");
        auto* objB = doc->addObject("App::FeaturePython", "ObjB");
        auto* objC = doc->addObject("App::FeaturePython", "ObjC");
        objA->Visibility.setValue(true);
        objB->Visibility.setValue(true);
        objC->Visibility.setValue(false);

        QVERIFY(!docItem->isIsolated());

        docItem->isolateObjects({objA});

        QVERIFY(docItem->isIsolated());
        QCOMPARE(docItem->getIsolatedObjects(), std::set<App::DocumentObject*> {objA});
        QVERIFY(objA->Visibility.getValue());
        QVERIFY(!objB->Visibility.getValue());
        QVERIFY(!objC->Visibility.getValue());

        docItem->unisolate();

        QVERIFY(!docItem->isIsolated());
        QVERIFY(docItem->getIsolatedObjects().empty());
        QVERIFY(objA->Visibility.getValue());
        QVERIFY(objB->Visibility.getValue());
        QVERIFY(!objC->Visibility.getValue());
    }

    void isolateGroupWithDescendants_PreservesDescendantVisibility()
    {
        auto* group = static_cast<App::DocumentObjectGroup*>(
            doc->addObject("App::DocumentObjectGroup", "Group")
        );
        auto* childVisible = doc->addObject("App::FeaturePython", "ChildVisible");
        auto* childHidden = doc->addObject("App::FeaturePython", "ChildHidden");
        auto* unrelated = doc->addObject("App::FeaturePython", "Unrelated");

        group->addObject(childVisible);
        group->addObject(childHidden);
        group->Visibility.setValue(true);
        childVisible->Visibility.setValue(true);
        childHidden->Visibility.setValue(false);
        unrelated->Visibility.setValue(true);

        docItem->isolateObjects({group});

        QVERIFY(docItem->isIsolated());
        QVERIFY(group->Visibility.getValue());
        QVERIFY(childVisible->Visibility.getValue());
        QVERIFY(!childHidden->Visibility.getValue());
        QVERIFY(!unrelated->Visibility.getValue());

        docItem->unisolate();

        QVERIFY(!docItem->isIsolated());
        QVERIFY(group->Visibility.getValue());
        QVERIFY(childVisible->Visibility.getValue());
        QVERIFY(!childHidden->Visibility.getValue());
        QVERIFY(unrelated->Visibility.getValue());
    }

    void isolateChildInsideContainer_KeepsAncestorContainerVisibleAndHidesSiblings()
    {
        auto* topGroup = static_cast<App::DocumentObjectGroup*>(
            doc->addObject("App::DocumentObjectGroup", "TopContainer")
        );
        auto* group = static_cast<App::DocumentObjectGroup*>(
            doc->addObject("App::DocumentObjectGroup", "SubGroup")
        );
        auto* child1 = doc->addObject("App::FeaturePython", "Child1");
        auto* child2 = doc->addObject("App::FeaturePython", "Child2");
        auto* otherTop = doc->addObject("App::FeaturePython", "OtherTop");

        topGroup->addObject(group);
        group->addObject(child1);
        group->addObject(child2);

        topGroup->Visibility.setValue(true);
        group->Visibility.setValue(true);
        child1->Visibility.setValue(true);
        child2->Visibility.setValue(true);
        otherTop->Visibility.setValue(true);

        docItem->isolateObjects({child1});

        QVERIFY(docItem->isIsolated());
        QVERIFY(topGroup->Visibility.getValue());
        QVERIFY(group->Visibility.getValue());
        QVERIFY(child1->Visibility.getValue());
        QVERIFY(!child2->Visibility.getValue());
        QVERIFY(!otherTop->Visibility.getValue());

        docItem->unisolate();

        QVERIFY(!docItem->isIsolated());
        QVERIFY(topGroup->Visibility.getValue());
        QVERIFY(group->Visibility.getValue());
        QVERIFY(child1->Visibility.getValue());
        QVERIFY(child2->Visibility.getValue());
        QVERIFY(otherTop->Visibility.getValue());
    }

    void switchIsolatedComponent_UnisolatesCurrentThenIsolatesNew()
    {
        auto* objA = doc->addObject("App::FeaturePython", "ObjA");
        auto* objB = doc->addObject("App::FeaturePython", "ObjB");
        auto* objC = doc->addObject("App::FeaturePython", "ObjC");
        objA->Visibility.setValue(true);
        objB->Visibility.setValue(true);
        objC->Visibility.setValue(false);

        // First isolate ObjA
        docItem->isolateObjects({objA});
        QVERIFY(docItem->isIsolated());
        QCOMPARE(docItem->getIsolatedObjects(), std::set<App::DocumentObject*> {objA});
        QVERIFY(objA->Visibility.getValue());
        QVERIFY(!objB->Visibility.getValue());
        QVERIFY(!objC->Visibility.getValue());

        // Now isolate ObjB while ObjA is still isolated:
        // must unisolate ObjA first and then isolate ObjB
        docItem->isolateObjects({objB});
        QVERIFY(docItem->isIsolated());
        QCOMPARE(docItem->getIsolatedObjects(), std::set<App::DocumentObject*> {objB});
        QVERIFY(!objA->Visibility.getValue());
        QVERIFY(objB->Visibility.getValue());
        QVERIFY(!objC->Visibility.getValue());

        // Finally unisolate: original pre-isolation visibilities must be restored
        docItem->unisolate();
        QVERIFY(!docItem->isIsolated());
        QVERIFY(objA->Visibility.getValue());
        QVERIFY(objB->Visibility.getValue());
        QVERIFY(!objC->Visibility.getValue());
    }

    void treeActionToggleAndRootAlwaysUnisolate()
    {
        auto* objA = doc->addObject("App::FeaturePython", "ObjA");
        auto* objB = doc->addObject("App::FeaturePython", "ObjB");
        objA->Visibility.setValue(true);
        objB->Visibility.setValue(true);

        // Ensure tree items are created
        Gui::TreeWidget::updateStatus(false);

        Gui::DocumentObjectItem* itemA = nullptr;
        Gui::DocumentObjectItem* itemB = nullptr;
        for (int i = 0; i < docItem->childCount(); ++i) {
            auto* child = docItem->child(i);
            if (child->type() == Gui::TreeWidget::ObjectType) {
                auto* objItem = static_cast<Gui::DocumentObjectItem*>(child);
                if (objItem->object()->getObject() == objA) {
                    itemA = objItem;
                }
                else if (objItem->object()->getObject() == objB) {
                    itemB = objItem;
                }
            }
        }
        QVERIFY(itemA != nullptr);
        QVERIFY(itemB != nullptr);

        // 1. Root DocumentItem should always say "Unisolate" and be disabled when not isolated
        tree->updateIsolateAction(docItem);
        QCOMPARE(tree->getIsolateAction()->text(), QStringLiteral("Unisolate"));
        QVERIFY(!tree->getIsolateAction()->isEnabled());

        // 2. Object itemA should say "Isolate" when not isolated
        QSignalBlocker selBlocker(tree.get());
        tree->clearSelection();
        itemA->setSelected(true);
        tree->updateIsolateAction(itemA);
        QCOMPARE(tree->getIsolateAction()->text(), QStringLiteral("Isolate"));
        QVERIFY(!tree->getIsolateAction()->icon().isNull());
        QVERIFY(tree->getIsolateAction()->isEnabled());

        // Trigger isolate on itemA
        tree->getIsolateAction()->trigger();
        QVERIFY(docItem->isIsolated());
        QVERIFY(objA->Visibility.getValue());
        QVERIFY(!objB->Visibility.getValue());

        // 3. Object itemA should now say "Unisolate" since it is the isolated item
        tree->updateIsolateAction(itemA);
        QCOMPARE(tree->getIsolateAction()->text(), QStringLiteral("Unisolate"));
        QVERIFY(!tree->getIsolateAction()->icon().isNull());
        QVERIFY(tree->getIsolateAction()->isEnabled());

        // 4. Object itemB (different component) should say "Isolate"
        tree->clearSelection();
        itemB->setSelected(true);
        tree->updateIsolateAction(itemB);
        QCOMPARE(tree->getIsolateAction()->text(), QStringLiteral("Isolate"));
        QVERIFY(tree->getIsolateAction()->isEnabled());

        // Triggering on itemB switches isolation from A to B
        tree->getIsolateAction()->trigger();
        QVERIFY(docItem->isIsolated());
        QCOMPARE(docItem->getIsolatedObjects(), std::set<App::DocumentObject*> {objB});
        QVERIFY(!objA->Visibility.getValue());
        QVERIFY(objB->Visibility.getValue());

        // 5. Root DocumentItem should say "Unisolate" and be enabled when isolated
        tree->clearSelection();
        docItem->setSelected(true);
        tree->updateIsolateAction(docItem);
        QCOMPARE(tree->getIsolateAction()->text(), QStringLiteral("Unisolate"));
        QVERIFY(tree->getIsolateAction()->isEnabled());

        // Triggering on root DocumentItem unisolates everything
        tree->getIsolateAction()->trigger();
        QVERIFY(!docItem->isIsolated());
        QVERIFY(objA->Visibility.getValue());
        QVERIFY(objB->Visibility.getValue());
        tree->clearSelection();
    }

    void deleteIsolatedObject_CleansUpGracefully()
    {
        auto* objA = doc->addObject("App::FeaturePython", "ObjA");
        auto* objB = doc->addObject("App::FeaturePython", "ObjB");
        objA->Visibility.setValue(true);
        objB->Visibility.setValue(true);

        Gui::TreeWidget::updateStatus(false);

        docItem->isolateObjects({objA});
        QVERIFY(docItem->isIsolated());
        QVERIFY(!objB->Visibility.getValue());

        doc->removeObject(objA->getNameInDocument());

        QVERIFY(!docItem->isIsolated());
        docItem->unisolate();
    }

    void isolateChildReferencedByLinkOrArrayInAnotherAssembly_HidesOtherAssembly()
    {
        // Mirrors the Voron 2.5 structure:
        // Sub-assemblies (DocumentObjectGroup, Vis=false)
        //   |- spool_cell (DocumentObjectGroup, Vis=false)
        //   |    |- spool_mount (FeaturePython, Vis=true)
        //   |    |- filament_spool (FeaturePython, Vis=true)
        //   |- filament_chamber (DocumentObjectGroup, Vis=true)
        //        |- filament_spools_array (FeaturePython with PropertyLink Base -> spool_cell, Vis=true)
        auto* subAssemblies = static_cast<App::DocumentObjectGroup*>(
            doc->addObject("App::DocumentObjectGroup", "SubAssemblies")
        );
        auto* spoolCell = static_cast<App::DocumentObjectGroup*>(
            doc->addObject("App::DocumentObjectGroup", "spool_cell")
        );
        auto* spoolMount = doc->addObject("App::FeaturePython", "spool_mount");
        auto* filamentSpool = doc->addObject("App::FeaturePython", "filament_spool");
        auto* filamentChamber = static_cast<App::DocumentObjectGroup*>(
            doc->addObject("App::DocumentObjectGroup", "filament_chamber")
        );
        auto* spoolsArray = doc->addObject("App::FeaturePython", "filament_spools_array");
        auto* baseProp = static_cast<App::PropertyLink*>(
            spoolsArray->addDynamicProperty("App::PropertyLink", "Base")
        );
        baseProp->setValue(spoolCell);

        spoolCell->addObject(spoolMount);
        spoolCell->addObject(filamentSpool);
        filamentChamber->addObject(spoolsArray);
        subAssemblies->addObject(spoolCell);
        subAssemblies->addObject(filamentChamber);

        subAssemblies->Visibility.setValue(false);
        spoolCell->Visibility.setValue(false);
        spoolMount->Visibility.setValue(true);
        filamentSpool->Visibility.setValue(true);
        filamentChamber->Visibility.setValue(true);
        spoolsArray->Visibility.setValue(true);

        Gui::TreeWidget::updateStatus(false);

        docItem->isolateObjects({spoolMount});

        QVERIFY(docItem->isIsolated());
        QVERIFY(subAssemblies->Visibility.getValue());
        QVERIFY(spoolCell->Visibility.getValue());
        QVERIFY(spoolMount->Visibility.getValue());
        QVERIFY(!filamentSpool->Visibility.getValue());
        QVERIFY(!filamentChamber->Visibility.getValue());
        QVERIFY(!spoolsArray->Visibility.getValue());

        docItem->unisolate();

        QVERIFY(!docItem->isIsolated());
        QVERIFY(!subAssemblies->Visibility.getValue());
        QVERIFY(!spoolCell->Visibility.getValue());
        QVERIFY(spoolMount->Visibility.getValue());
        QVERIFY(filamentSpool->Visibility.getValue());
        QVERIFY(filamentChamber->Visibility.getValue());
        QVERIFY(spoolsArray->Visibility.getValue());
    }

private:
    std::unique_ptr<Gui::TreeWidget> tree;
    std::string docName;
    App::Document* doc = nullptr;
    Gui::Document* guiDoc = nullptr;
    Gui::DocumentItem* docItem = nullptr;
};

QTEST_MAIN(TreeIsolateTest)
#include "TreeIsolate.moc"
