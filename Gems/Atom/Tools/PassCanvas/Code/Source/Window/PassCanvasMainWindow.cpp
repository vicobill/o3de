/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AtomToolsFramework/SettingsDialog/SettingsDialog.h>
#include <AzCore/IO/FileIO.h>
#include <AzQtComponents/Components/StyleManager.h>
#include <GraphCanvas/Widgets/NodePalette/TreeItems/NodePaletteTreeItem.h>
#include <Window/PassCanvasMainWindow.h>
#include <Window/PassCanvasViewportContent.h>

#include <QMessageBox>

namespace PassCanvas
{
    PassCanvasMainWindow::PassCanvasMainWindow(
        const AZ::Crc32& toolId, AtomToolsFramework::GraphViewSettingsPtr graphViewSettingsPtr, QWidget* parent)
        : Base(toolId, "PassCanvasMainWindow", parent)
        , m_graphViewSettingsPtr(graphViewSettingsPtr)
        , m_styleManager(toolId, graphViewSettingsPtr->m_styleManagerPath)
    {
        m_assetBrowser->SetFilterState("", AZ::RPI::StreamingImageAsset::Group, true);
        m_assetBrowser->SetFilterState("", AZ::RPI::PassAsset::Group, true);

        m_documentInspector = new AtomToolsFramework::AtomToolsDocumentInspector(m_toolId, this);
        m_documentInspector->SetDocumentSettingsPrefix("/O3DE/Atom/PassCanvas/DocumentInspector");
        AddDockWidget("Inspector", m_documentInspector, Qt::RightDockWidgetArea);

        // Set up the toolbar that controls the viewport settings
        m_toolBar = new AtomToolsFramework::EntityPreviewViewportToolBar(m_toolId, this);

        // Create the dockable viewport widget that will be shared between all Pass Canvas documents
        m_passViewport = new AtomToolsFramework::EntityPreviewViewportWidget(m_toolId, this);

        // Initialize the entity context that will be used to create all of the entities displayed in the viewport
        auto entityContext = AZStd::make_shared<AzFramework::EntityContext>();
        entityContext->InitContext();

        // Initialize the atom scene and pipeline that will bind to the viewport window to render entities and presets
        auto viewportScene = AZStd::make_shared<AtomToolsFramework::EntityPreviewViewportScene>(
            m_toolId, m_passViewport, entityContext, "PassCanvasViewportWidget", "passes/mainrenderpipeline.azasset");

        // Viewport content will instantiate all of the entities that will be displayed and controlled by the viewport
        auto viewportContent = AZStd::make_shared<PassCanvasViewportContent>(m_toolId, m_passViewport, entityContext);

        // The input controller creates and binds input behaviors to control viewport objects
        auto viewportController = AZStd::make_shared<AtomToolsFramework::EntityPreviewViewportInputController>(m_toolId, m_passViewport, viewportContent);

        // Inject the entity context, scene, content, and controller into the viewport widget
        m_passViewport->Init(entityContext, viewportScene, viewportContent, viewportController);

        // Combine the shared toolbar in viewport into stacked widget that will be docked as a single view
        auto viewPortAndToolbar = new QWidget(this);
        viewPortAndToolbar->setLayout(new QVBoxLayout(viewPortAndToolbar));
        viewPortAndToolbar->layout()->setContentsMargins(0, 0, 0, 0);
        viewPortAndToolbar->layout()->setMargin(0);
        viewPortAndToolbar->layout()->setSpacing(0);
        viewPortAndToolbar->layout()->addWidget(m_toolBar);
        viewPortAndToolbar->layout()->addWidget(m_passViewport);

        AddDockWidget("Viewport", viewPortAndToolbar, Qt::BottomDockWidgetArea);

        m_viewportSettingsInspector = new AtomToolsFramework::EntityPreviewViewportSettingsInspector(m_toolId, this);
        AddDockWidget("Viewport Settings", m_viewportSettingsInspector, Qt::LeftDockWidgetArea);
        SetDockWidgetVisible("Viewport Settings", false);

        m_bookmarkDockWidget = aznew GraphCanvas::BookmarkDockWidget(m_toolId, this);
        AddDockWidget("Bookmarks", m_bookmarkDockWidget, Qt::BottomDockWidgetArea);
        SetDockWidgetVisible("Bookmarks", false);

        AddDockWidget("MiniMap", aznew GraphCanvas::MiniMapDockWidget(m_toolId, this), Qt::BottomDockWidgetArea);
        SetDockWidgetVisible("MiniMap", false);

        GraphCanvas::NodePaletteConfig nodePaletteConfig;
        nodePaletteConfig.m_rootTreeItem = m_graphViewSettingsPtr->m_createNodeTreeItemsFn(m_toolId);
        nodePaletteConfig.m_editorId = m_toolId;
        nodePaletteConfig.m_mimeType = m_graphViewSettingsPtr->m_nodeMimeType.c_str();
        nodePaletteConfig.m_isInContextMenu = false;
        nodePaletteConfig.m_saveIdentifier = m_graphViewSettingsPtr->m_nodeSaveIdentifier;

        m_nodePalette = aznew GraphCanvas::NodePaletteDockWidget(this, "Node Palette", nodePaletteConfig);
        AddDockWidget("Node Palette", m_nodePalette, Qt::LeftDockWidgetArea);

        AZ::IO::FixedMaxPath resolvedPath;
        AZ::IO::FileIOBase::GetInstance()->ReplaceAlias(resolvedPath, m_graphViewSettingsPtr->m_translationPath.c_str());
        const AZ::IO::FixedMaxPathString translationFilePath = resolvedPath.LexicallyNormal().FixedMaxPathString();
        if (m_translator.load(QLocale::Language::English, translationFilePath.c_str()))
        {
            if (!qApp->installTranslator(&m_translator))
            {
                AZ_Warning("PassCanvas", false, "Error installing translation %s!", translationFilePath.c_str());
            }
        }
        else
        {
            AZ_Warning("PassCanvas", false, "Error loading translation file %s", translationFilePath.c_str());
        }

        // Set up style sheet to fix highlighting in the node palette
        AzQtComponents::StyleManager::setStyleSheet(this, QStringLiteral(":/GraphView/GraphView.qss"));

        OnDocumentOpened(AZ::Uuid::CreateNull());
    }

    void PassCanvasMainWindow::OnDocumentOpened(const AZ::Uuid& documentId)
    {
        Base::OnDocumentOpened(documentId);
        m_documentInspector->SetDocumentId(documentId);
    }

    void PassCanvasMainWindow::ResizeViewportRenderTarget(uint32_t width, uint32_t height)
    {
        QSize requestedViewportSize = QSize(width, height) / devicePixelRatioF();
        QSize currentViewportSize = m_passViewport->size();
        QSize offset = requestedViewportSize - currentViewportSize;
        QSize requestedWindowSize = size() + offset;
        resize(requestedWindowSize);

        AZ_Assert(
            m_passViewport->size() == requestedViewportSize,
            "Resizing the window did not give the expected viewport size. Requested %d x %d but got %d x %d.",
            requestedViewportSize.width(), requestedViewportSize.height(), m_passViewport->size().width(),
            m_passViewport->size().height());

        [[maybe_unused]] QSize newDeviceSize = m_passViewport->size();
        AZ_Warning(
            "Pass Canvas",
            static_cast<uint32_t>(newDeviceSize.width()) == width && static_cast<uint32_t>(newDeviceSize.height()) == height,
            "Resizing the window did not give the expected frame size. Requested %d x %d but got %d x %d.", width, height,
            newDeviceSize.width(), newDeviceSize.height());
    }

    void PassCanvasMainWindow::LockViewportRenderTargetSize(uint32_t width, uint32_t height)
    {
        m_passViewport->LockRenderTargetSize(width, height);
    }

    void PassCanvasMainWindow::UnlockViewportRenderTargetSize()
    {
        m_passViewport->UnlockRenderTargetSize();
    }

    void PassCanvasMainWindow::PopulateSettingsInspector(AtomToolsFramework::InspectorWidget* inspector) const
    {
        m_passCanvasCompileSettingsGroup = AtomToolsFramework::CreateSettingsPropertyGroup(
            "Pass Canvas Settings",
            "Pass Canvas Settings",
            { AtomToolsFramework::CreateSettingsPropertyValue(
                  "/O3DE/Atom/PassCanvas/EnableFasterShaderBuilds",
                  "Enable Faster Shader Builds",
                  "By default, some platforms perform an exhaustive compilation of shaders for multiple RHI. For example, the default "
                  "Windows shader builder settings automatically compiles shaders for DX12, Vulkan, and the Null renderer.\n\nThis option "
                  "overrides those registry settings and makes compilation and preview times much faster by only compiling shaders for the "
                  "currently active platform and RHI.\n\nThis also disables automatic shader variant generation.\n\nChanging this setting "
                  "requires restarting Pass Canvas and the Asset Processor.\n\nChanging the active RHI with this setting enabled may "
                  "require clearing the cache to regenerate shaders for the new RHI.\n\nThe settings files containing the overrides will be "
                  "placed in the user/Registry folder for the current project.",
                  false),
              AtomToolsFramework::CreateSettingsPropertyValue(
                  "/O3DE/Atom/PassCanvas/Viewport/ClearPassOnCompileGraphStarted",
                  "Clear Viewport Pass When Compiling Starts",
                  "Clear the viewport model's pass whenever compiling shaders and passes starts.",
                  true),
              AtomToolsFramework::CreateSettingsPropertyValue(
                  "/O3DE/Atom/PassCanvas/Viewport/ClearPassOnCompileGraphFailed",
                  "Clear Viewport Pass When Compiling Fails",
                  "Clear the viewport model's pass whenever compiling shaders and passes fails.",
                  true),
              AtomToolsFramework::CreateSettingsPropertyValue(
                  "/O3DE/Atom/PassCanvas/CreateDefaultDocumentOnStart",
                  "Create Untitled Graph Document On Start",
                  "Create a default, untitled graph document when Pass Canvas starts",
                  true) });

        inspector->AddGroup(
            m_passCanvasCompileSettingsGroup->m_name,
            m_passCanvasCompileSettingsGroup->m_displayName,
            m_passCanvasCompileSettingsGroup->m_description,
            new AtomToolsFramework::InspectorPropertyGroupWidget(
                m_passCanvasCompileSettingsGroup.get(),
                m_passCanvasCompileSettingsGroup.get(),
                azrtti_typeid<AtomToolsFramework::DynamicPropertyGroup>()));

        inspector->AddGroup(
            "Graph View Settings",
            "Graph View Settings",
            "Configuration settings for the graph view interaction, animation, and other behavior.",
            new AtomToolsFramework::InspectorPropertyGroupWidget(
                m_graphViewSettingsPtr.get(), m_graphViewSettingsPtr.get(), m_graphViewSettingsPtr->RTTI_Type()));

        Base::PopulateSettingsInspector(inspector);
    }

    AZStd::string PassCanvasMainWindow::GetHelpDialogText() const
    {
        return R"(<html><head/><body>
            <p><h3><u>Shader Build Settings</u></h3></p>
            <p>Shaders, passes, and other assets will be generated as changes are applied to the graph.
            The viewport will update and display the generated passes and shaders once they have been
            compiled by the Asset Processor. This can take a few seconds. Compilation times and preview
            responsiveness can be improved by enabling the Minimal Shader Build settings in the Tools->Settings
            menu. Changing the settings will require restarting Pass Canvas and the Asset Processor.</p>
            <p><h3><u>Camera Controls</u></h3></p>
            <p><b>LMB</b> - rotate camera</p>
            <p><b>RMB</b> or <b>Alt+LMB</b> - orbit camera around target</p>
            <p><b>MMB</b> - pan camera on its xy plane</p>
            <p><b>Alt+RMB</b> or <b>LMB+RMB</b> - dolly camera on its z axis</p>
            <p><b>Ctrl+LMB</b> - rotate model</p>
            <p><b>Shift+LMB</b> - rotate environment</p>
            </body></html>)";
    }
} // namespace PassCanvas

#include <Window/moc_PassCanvasMainWindow.cpp>
