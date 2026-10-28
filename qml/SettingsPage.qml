import QtQuick

Item {
    id: root
    property string folderMsg: ""
    property var dialog: null

    function addPath(p) { folderMsg = Library.addFolder(p); if (folderMsg === "") pathInput.text = "" }
    function browse() {
        if (!dialog) {
            try {
                dialog = Qt.createQmlObject('import QtQuick.Dialogs\nFolderDialog { title: "Choose a music folder" }', root)
                dialog.accepted.connect(function () { root.addPath(dialog.selectedFolder.toString()) })
            } catch (e) {
                folderMsg = "The folder picker isn't available on this system. Paste the path, or drop a folder on the window."
                return
            }
        }
        dialog.open()
    }

    Flickable {
        id: scroller
        anchors.fill: parent
        contentWidth: width
        contentHeight: col.height + 32
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: col
            x: 28
            width: scroller.width - 56
            spacing: 10

            AppearanceSection { width: parent.width }
            Item { width: 1; height: 18 }

            // ================= Online catalog + algorithm =================
            Text { text: "Online music"; font.pixelSize: Math.round(20 * Pal.fs); font.weight: Font.DemiBold; color: Pal.onSurface }
            Text {
                width: parent.width; wrapMode: Text.WordWrap
                text: Catalog.online ? "Search any artist to browse their songs and albums. Songs start playing right away."
                                     : "Can't reach the online catalog right now. Your own library still works."
                font.pixelSize: Math.round(14 * Pal.fs); color: Catalog.online ? Pal.onSurfaceVariant : Pal.error
            }
            Item {
                width: parent.width; height: 52
                Column {
                    anchors.left: parent.left; anchors.right: explicitSwitch.left; anchors.rightMargin: 16
                    anchors.verticalCenter: parent.verticalCenter; spacing: 3
                    Text { text: "Prefer explicit versions"; font.pixelSize: Math.round(14 * Pal.fs); font.weight: Font.Medium; color: Pal.onSurface }
                    Text { text: "When both clean and explicit releases are available"; font.pixelSize: Math.round(12 * Pal.fs); color: Pal.onSurfaceVariant }
                }
                Switch { id: explicitSwitch; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                         checked: Catalog.preferExplicit; onToggled: (on) => Catalog.setPreferExplicit(on) }
            }
            Text {
                width: parent.width; wrapMode: Text.WordWrap
                text: Resolver.hasFullSource ? "Full-length playback from: " + Resolver.fullSources.join(", ") + ". Songs you own as files always play from the file."
                                              : "Without a full-length source, online songs play as 30-second previews. Songs you also have as files in your library play in full from the file. You can switch on a source extension under Extensions below."
                font.pixelSize: Math.round(13 * Pal.fs); color: Pal.outline
            }
            Item { width: 1; height: 8 }
            AlgorithmSection { width: parent.width }
            Item { width: 1; height: 18 }

            // ================= Library =================
            Text { text: "Library folders"; font.pixelSize: Math.round(20 * Pal.fs); font.weight: Font.DemiBold; color: Pal.onSurface }
            Text {
                width: parent.width; wrapMode: Text.WordWrap
                text: "Lumen reads these folders, including subfolders, and remembers what it found. Only new or changed files are read on later starts."
                font.pixelSize: Math.round(14 * Pal.fs); color: Pal.onSurfaceVariant
            }

            Repeater {
                model: Library.folders
                delegate: Rectangle {
                    width: col.width; height: 64; radius: 20 * Pal.rs; color: Pal.surfaceHigh
                    Icon { id: fi; x: 18; anchors.verticalCenter: parent.verticalCenter; name: "folder"; color: modelData.exists ? Pal.primary : Pal.error }
                    Column {
                        anchors.left: fi.right; anchors.leftMargin: 14
                        anchors.right: del.left; anchors.rightMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        Text { width: parent.width; text: modelData.path; elide: Text.ElideMiddle; font.pixelSize: Math.round(14 * Pal.fs); font.weight: Font.Medium; color: Pal.onSurface }
                        Text {
                            width: parent.width
                            text: !modelData.exists ? "Folder not found (is the drive mounted?)"
                                  : modelData.count + (modelData.count === 1 ? " song" : " songs")
                            font.pixelSize: Math.round(13 * Pal.fs); color: modelData.exists ? Pal.onSurfaceVariant : Pal.error
                        }
                    }
                    IconButton {
                        id: del
                        anchors.right: parent.right; anchors.rightMargin: 10; anchors.verticalCenter: parent.verticalCenter
                        icon: "delete"
                        onClicked: Library.removeFolder(modelData.path)
                    }
                }
            }
            Text {
                visible: Library.folders.length === 0
                text: "No folders yet."; font.pixelSize: Math.round(14 * Pal.fs); color: Pal.onSurfaceVariant
            }

            Row {
                width: parent.width
                spacing: 8
                Rectangle {
                    width: parent.width - addBtn.width - browseBtn.width - 16
                    height: 48; radius: 24 * Pal.rs; color: Pal.surfaceHigh
                    border.width: pathInput.activeFocus ? 2 : 0; border.color: Pal.primary
                    TextInput {
                        id: pathInput
                        anchors.fill: parent; anchors.leftMargin: 20; anchors.rightMargin: 20
                        verticalAlignment: TextInput.AlignVCenter
                        font.pixelSize: Math.round(15 * Pal.fs); color: Pal.onSurface; clip: true; selectByMouse: true
                        Accessible.name: "Folder path"
                        onAccepted: root.addPath(text)
                    }
                    Text {
                        anchors.fill: pathInput; verticalAlignment: Text.AlignVCenter
                        visible: pathInput.text === ""
                        text: "Paste a folder path, e.g. ~/Music"
                        font.pixelSize: Math.round(15 * Pal.fs); color: Pal.onSurfaceVariant
                    }
                }
                Pill { id: browseBtn; icon: "folder_open"; text: "Browse"; filled: false; onClicked: root.browse() }
                Pill { id: addBtn; icon: "add"; text: "Add"; onClicked: root.addPath(pathInput.text) }
            }
            Text {
                width: parent.width; wrapMode: Text.WordWrap
                visible: root.folderMsg !== ""
                text: root.folderMsg; font.pixelSize: Math.round(13 * Pal.fs); color: Pal.error
            }
            Row {
                spacing: 12
                Pill { icon: "refresh"; text: Library.scanning ? "Scanning\u2026 " + Library.scanned : "Rescan now"; filled: false
                       enabled: !Library.scanning; opacity: enabled ? 1 : 0.6; onClicked: Library.rescan() }
                Text { anchors.verticalCenter: parent.verticalCenter; text: "You can also drop a folder onto the window."
                       font.pixelSize: Math.round(13 * Pal.fs); color: Pal.onSurfaceVariant }
            }

            Item { width: 1; height: 18 }

            // ================= Extensions =================
            Text { text: "Extensions"; font.pixelSize: Math.round(20 * Pal.fs); font.weight: Font.DemiBold; color: Pal.onSurface }
            Text {
                width: parent.width; wrapMode: Text.WordWrap
                text: "Extensions add lyrics and online sources. Each one runs as its own small process that starts when needed and stops when idle. Official extensions are held to 20 MB of memory."
                font.pixelSize: Math.round(14 * Pal.fs); color: Pal.onSurfaceVariant
            }
            Pill { text: "Copy official extensions to user folder"; filled: false
                   onClicked: Bus.toast(Plugins.installOfficialExtensions()) }

            Repeater {
                model: Plugins.list
                delegate: Rectangle {
                    id: card
                    property bool confirmRemove: false
                    readonly property string pid: modelData.id
                    width: col.width
                    height: cardCol.height + 36
                    radius: 24 * Pal.rs
                    color: Pal.surfaceHigh

                    Rectangle {
                        id: badge
                        x: 18; y: 18; width: 44; height: 44; radius: 16 * Pal.rs
                        color: modelData.enabled ? Pal.primaryContainer : Pal.surfaceHighest
                        Icon { anchors.centerIn: parent; name: "extension"; size: 24
                               color: modelData.enabled ? Pal.onPrimaryContainer : Pal.onSurfaceVariant }
                    }
                    Column {
                        id: cardCol
                        anchors.left: badge.right; anchors.leftMargin: 16
                        anchors.right: ctl.left; anchors.rightMargin: 12
                        y: 18
                        spacing: 6
                        Row {
                            spacing: 8
                            Text { text: modelData.name; font.pixelSize: Math.round(16 * Pal.fs); font.weight: Font.DemiBold; color: Pal.onSurface }
                            Text { anchors.baseline: parent.children[0].baseline; text: modelData.version; font.pixelSize: Math.round(13 * Pal.fs); color: Pal.onSurfaceVariant }
                        }
                        Text {
                            width: parent.width; wrapMode: Text.WordWrap
                            visible: modelData.description !== ""
                            text: modelData.description; font.pixelSize: Math.round(13 * Pal.fs); color: Pal.onSurfaceVariant
                        }
                        Flow {
                            width: parent.width; spacing: 6
                            Chip { text: modelData.official ? "Official" : "Community"; accent: modelData.official }
                            Repeater { model: modelData.capabilities; delegate: Chip { text: modelData } }
                        }
                        Text {
                            width: parent.width; wrapMode: Text.WordWrap
                            visible: !modelData.official
                            text: "Runs with your permissions. Use at your own risk; it may need setup and some knowledge."
                            font.pixelSize: Math.round(12 * Pal.fs); color: Pal.onSurfaceVariant
                        }
                        Text {
                            width: parent.width; wrapMode: Text.WordWrap
                            visible: modelData.setup !== ""
                            text: "Setup: " + modelData.setup; font.pixelSize: Math.round(12 * Pal.fs); color: Pal.outline
                        }
                        Repeater {
                            model: modelData.options || []
                            delegate: Column {
                                id: optCol
                                width: cardCol.width
                                spacing: 6
                                readonly property var opt: modelData
                                Text { text: optCol.opt.label; font.pixelSize: Math.round(13 * Pal.fs); font.weight: Font.Medium; color: Pal.onSurface }
                                Flow {
                                    visible: optCol.opt.type === "choice"
                                    width: parent.width; spacing: 6
                                    Repeater {
                                        model: optCol.opt.choices
                                        delegate: FilterChip {
                                            text: modelData.label
                                            checked: optCol.opt.value === modelData.value
                                            onClicked: Plugins.setOption(card.pid, optCol.opt.key, modelData.value)
                                        }
                                    }
                                }
                                Rectangle {
                                    visible: optCol.opt.type !== "choice"
                                    width: parent.width; height: 40; radius: 20 * Pal.rs
                                    color: Pal.surfaceHighest
                                    border.width: optInput.activeFocus ? 2 : 0
                                    border.color: Pal.primary
                                    TextInput {
                                        id: optInput
                                        anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 16
                                        verticalAlignment: TextInput.AlignVCenter
                                        font.pixelSize: Math.round(13 * Pal.fs); color: Pal.onSurface
                                        clip: true; selectByMouse: true
                                        text: optCol.opt.value
                                        Accessible.name: optCol.opt.label
                                        onEditingFinished: if (text !== optCol.opt.value) Plugins.setOption(card.pid, optCol.opt.key, text)
                                    }
                                }
                                Text {
                                    width: parent.width; wrapMode: Text.WordWrap
                                    visible: (optCol.opt.hint || "") !== ""
                                    text: optCol.opt.hint; font.pixelSize: Math.round(12 * Pal.fs); color: Pal.outline
                                }
                            }
                        }
                        Row {
                            spacing: 8
                            visible: card.confirmRemove
                            Text { anchors.verticalCenter: parent.verticalCenter; text: "Remove this extension?"; font.pixelSize: Math.round(13 * Pal.fs); color: Pal.onSurface }
                            Pill { implicitHeight: 36; text: "Remove"; onClicked: Plugins.uninstall(modelData.id) }
                            Pill { implicitHeight: 36; text: "Keep"; filled: false; onClicked: card.confirmRemove = false }
                        }
                        Text {
                            width: parent.width; wrapMode: Text.WordWrap
                            visible: modelData.enabled
                            text: modelData.status + (modelData.running ? "  \u2022  " + modelData.memMB.toFixed(1) + " of " + modelData.memLimit + " MB" : "")
                            font.pixelSize: Math.round(12 * Pal.fs); color: Pal.outline
                        }
                    }
                    Row {
                        id: ctl
                        anchors.right: parent.right; anchors.rightMargin: 14; y: 14
                        spacing: 0
                        IconButton { icon: "arrow_upward"; iconSize: 20; width: 36; height: 36; onClicked: Plugins.move(modelData.id, -1) }
                        IconButton { icon: "arrow_downward"; iconSize: 20; width: 36; height: 36; onClicked: Plugins.move(modelData.id, 1) }
                        IconButton { visible: !modelData.official; icon: "delete"; iconSize: 20; width: 36; height: 36; onClicked: card.confirmRemove = true }
                        Item { width: 8; height: 1 }
                        Switch { anchors.verticalCenter: parent.verticalCenter; checked: modelData.enabled; onToggled: (on) => Plugins.setEnabled(modelData.id, on) }
                    }
                }
            }
            Text {
                visible: Plugins.list.length === 0
                width: parent.width; wrapMode: Text.WordWrap
                text: "No extensions found."; font.pixelSize: Math.round(14 * Pal.fs); color: Pal.onSurfaceVariant
            }
            Text {
                width: parent.width; wrapMode: Text.WordWrap
                text: "Order matters: lyrics providers are tried from the top down."
                font.pixelSize: Math.round(12 * Pal.fs); color: Pal.outline
                visible: Plugins.list.length > 1
            }
            Item { width: 1; height: 18 }
            Text { text: "Get more extensions"; font.pixelSize: Math.round(20 * Pal.fs); font.weight: Font.DemiBold; color: Pal.onSurface }
            Text {
                width: parent.width; wrapMode: Text.WordWrap
                text: "Extensions are listed in an index you choose. Each download is checked against the checksum in that index before it's unpacked, and nothing runs until you switch it on."
                font.pixelSize: Math.round(14 * Pal.fs); color: Pal.onSurfaceVariant
            }
            Row {
                width: parent.width; spacing: 8
                Rectangle {
                    width: parent.width - browseStore.width - 8; height: 48; radius: 24 * Pal.rs; color: Pal.surfaceHigh
                    border.width: indexInput.activeFocus ? 2 : 0; border.color: Pal.primary
                    TextInput {
                        id: indexInput
                        anchors.fill: parent; anchors.leftMargin: 20; anchors.rightMargin: 20
                        verticalAlignment: TextInput.AlignVCenter
                        font.pixelSize: Math.round(14 * Pal.fs); color: Pal.onSurface; clip: true; selectByMouse: true
                        text: Market.indexUrl
                        Accessible.name: "Extension index address"
                        onEditingFinished: Market.indexUrl = text
                        onAccepted: { Market.indexUrl = text; Market.refresh() }
                    }
                }
                Pill { id: browseStore; icon: "cloud"; text: Market.busy ? "Working\u2026" : "Browse"; enabled: !Market.busy
                       onClicked: { Market.indexUrl = indexInput.text; Market.refresh() } }
            }
            Text {
                width: parent.width; wrapMode: Text.WordWrap
                visible: Market.message !== ""
                text: Market.message; font.pixelSize: Math.round(13 * Pal.fs); color: Pal.onSurfaceVariant
            }
            Repeater {
                model: Market.items
                delegate: Rectangle {
                    id: sc
                    property bool confirming: false
                    readonly property var local: { var l = Plugins.list; for (var i = 0; i < l.length; ++i) if (l[i].id === modelData.id) return l[i]; return null }
                    readonly property bool installed: local !== null
                    readonly property bool outdated: installed && local.version !== modelData.version
                    width: col.width; height: scol.height + 36; radius: 24 * Pal.rs; color: Pal.surfaceHigh
                    Rectangle { id: sb; x: 18; y: 18; width: 44; height: 44; radius: 16 * Pal.rs; color: Pal.secondaryContainer
                                Icon { anchors.centerIn: parent; name: "cloud"; size: 24; color: Pal.onSecondaryContainer } }
                    Column {
                        id: scol
                        anchors.left: sb.right; anchors.leftMargin: 16; anchors.right: sact.left; anchors.rightMargin: 12; y: 18
                        spacing: 6
                        Row {
                            spacing: 8
                            Text { text: modelData.name; font.pixelSize: Math.round(16 * Pal.fs); font.weight: Font.DemiBold; color: Pal.onSurface }
                            Text { anchors.baseline: parent.children[0].baseline; text: modelData.version; font.pixelSize: Math.round(13 * Pal.fs); color: Pal.onSurfaceVariant }
                        }
                        Text { width: parent.width; wrapMode: Text.WordWrap; text: modelData.description || ""; font.pixelSize: Math.round(13 * Pal.fs); color: Pal.onSurfaceVariant }
                        Flow {
                            width: parent.width; spacing: 6
                            Chip { text: "Community" }
                            Repeater { model: modelData.capabilities || []; delegate: Chip { text: modelData } }
                        }
                        Text {
                            width: parent.width; wrapMode: Text.WordWrap
                            visible: (modelData.setup || "") !== ""
                            text: "Setup: " + modelData.setup; font.pixelSize: Math.round(12 * Pal.fs); color: Pal.outline
                        }
                        Text {
                            width: parent.width; wrapMode: Text.WordWrap
                            visible: sc.confirming
                            text: "This extension will run on your computer with your permissions. Only continue if you trust its author."
                            font.pixelSize: Math.round(13 * Pal.fs); color: Pal.error
                        }
                    }
                    Column {
                        id: sact
                        anchors.right: parent.right; anchors.rightMargin: 18; y: 18
                        spacing: 8
                        Pill {
                            implicitHeight: 40
                            visible: !sc.installed || sc.outdated
                            text: sc.confirming ? "Install anyway" : (sc.outdated ? "Update" : "Install")
                            enabled: !Market.busy
                            onClicked: { if (sc.confirming) { sc.confirming = false; Market.install(modelData.id) } else sc.confirming = true }
                        }
                        Pill { implicitHeight: 40; visible: sc.installed && !sc.outdated; text: "Installed"; filled: false; enabled: false; opacity: 0.7 }
                    }
                }
            }
            Row {
                spacing: 10
                Pill { icon: "folder_open"; text: "Open extensions folder"; filled: false; onClicked: Plugins.openUserDir() }
                Pill { icon: "refresh"; text: "Reload"; filled: false; onClicked: Plugins.rescan() }
            }
            TextEdit {
                width: parent.width; readOnly: true; selectByMouse: true; wrapMode: TextEdit.WrapAnywhere
                text: Plugins.userDir; font.pixelSize: Math.round(12 * Pal.fs); color: Pal.outline
            }
        }
    }
    ScrollIndicator { view: scroller; anchors.top: scroller.top; anchors.bottom: scroller.bottom; height: undefined }
}
