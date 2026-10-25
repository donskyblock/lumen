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

            // ================= Library =================
            Text { text: "Library folders"; font.pixelSize: 20; font.weight: Font.DemiBold; color: Pal.onSurface }
            Text {
                width: parent.width; wrapMode: Text.WordWrap
                text: "Lumen reads these folders, including subfolders, and remembers what it found. Only new or changed files are read on later starts."
                font.pixelSize: 14; color: Pal.onSurfaceVariant
            }

            Repeater {
                model: Library.folders
                delegate: Rectangle {
                    width: col.width; height: 64; radius: 20; color: Pal.surfaceHigh
                    Icon { id: fi; x: 18; anchors.verticalCenter: parent.verticalCenter; name: "folder"; color: modelData.exists ? Pal.primary : "#ffb4ab" }
                    Column {
                        anchors.left: fi.right; anchors.leftMargin: 14
                        anchors.right: del.left; anchors.rightMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        Text { width: parent.width; text: modelData.path; elide: Text.ElideMiddle; font.pixelSize: 14; font.weight: Font.Medium; color: Pal.onSurface }
                        Text {
                            width: parent.width
                            text: !modelData.exists ? "Folder not found (is the drive mounted?)"
                                  : modelData.count + (modelData.count === 1 ? " song" : " songs")
                            font.pixelSize: 13; color: modelData.exists ? Pal.onSurfaceVariant : "#ffb4ab"
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
                text: "No folders yet."; font.pixelSize: 14; color: Pal.onSurfaceVariant
            }

            Row {
                width: parent.width
                spacing: 8
                Rectangle {
                    width: parent.width - addBtn.width - browseBtn.width - 16
                    height: 48; radius: 24; color: Pal.surfaceHigh
                    border.width: pathInput.activeFocus ? 2 : 0; border.color: Pal.primary
                    TextInput {
                        id: pathInput
                        anchors.fill: parent; anchors.leftMargin: 20; anchors.rightMargin: 20
                        verticalAlignment: TextInput.AlignVCenter
                        font.pixelSize: 15; color: Pal.onSurface; clip: true; selectByMouse: true
                        Accessible.name: "Folder path"
                        onAccepted: root.addPath(text)
                    }
                    Text {
                        anchors.fill: pathInput; verticalAlignment: Text.AlignVCenter
                        visible: pathInput.text === ""
                        text: "Paste a folder path, e.g. ~/Music"
                        font.pixelSize: 15; color: Pal.onSurfaceVariant
                    }
                }
                Pill { id: browseBtn; icon: "folder_open"; text: "Browse"; filled: false; onClicked: root.browse() }
                Pill { id: addBtn; icon: "add"; text: "Add"; onClicked: root.addPath(pathInput.text) }
            }
            Text {
                width: parent.width; wrapMode: Text.WordWrap
                visible: root.folderMsg !== ""
                text: root.folderMsg; font.pixelSize: 13; color: "#ffb4ab"
            }
            Row {
                spacing: 12
                Pill { icon: "refresh"; text: Library.scanning ? "Scanning\u2026 " + Library.scanned : "Rescan now"; filled: false
                       enabled: !Library.scanning; opacity: enabled ? 1 : 0.6; onClicked: Library.rescan() }
                Text { anchors.verticalCenter: parent.verticalCenter; text: "You can also drop a folder onto the window."
                       font.pixelSize: 13; color: Pal.onSurfaceVariant }
            }

            Item { width: 1; height: 18 }

            // ================= Extensions =================
            Text { text: "Extensions"; font.pixelSize: 20; font.weight: Font.DemiBold; color: Pal.onSurface }
            Text {
                width: parent.width; wrapMode: Text.WordWrap
                text: "Extensions add lyrics and online sources. Each one runs as its own small process that starts when needed and stops when idle. Official extensions are held to 20 MB of memory."
                font.pixelSize: 14; color: Pal.onSurfaceVariant
            }

            Repeater {
                model: Plugins.list
                delegate: Rectangle {
                    id: card
                    property bool confirmRemove: false
                    width: col.width
                    height: cardCol.height + 36
                    radius: 24
                    color: Pal.surfaceHigh

                    Rectangle {
                        id: badge
                        x: 18; y: 18; width: 44; height: 44; radius: 16
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
                            Text { text: modelData.name; font.pixelSize: 16; font.weight: Font.DemiBold; color: Pal.onSurface }
                            Text { anchors.baseline: parent.children[0].baseline; text: modelData.version; font.pixelSize: 13; color: Pal.onSurfaceVariant }
                        }
                        Text {
                            width: parent.width; wrapMode: Text.WordWrap
                            visible: modelData.description !== ""
                            text: modelData.description; font.pixelSize: 13; color: Pal.onSurfaceVariant
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
                            font.pixelSize: 12; color: Pal.onSurfaceVariant
                        }
                        Text {
                            width: parent.width; wrapMode: Text.WordWrap
                            visible: modelData.setup !== ""
                            text: "Setup: " + modelData.setup; font.pixelSize: 12; color: Pal.outline
                        }
                        Row {
                            spacing: 8
                            visible: card.confirmRemove
                            Text { anchors.verticalCenter: parent.verticalCenter; text: "Remove this extension?"; font.pixelSize: 13; color: Pal.onSurface }
                            Pill { implicitHeight: 36; text: "Remove"; onClicked: Plugins.uninstall(modelData.id) }
                            Pill { implicitHeight: 36; text: "Keep"; filled: false; onClicked: card.confirmRemove = false }
                        }
                        Text {
                            width: parent.width; wrapMode: Text.WordWrap
                            visible: modelData.enabled
                            text: modelData.status + (modelData.running ? "  \u2022  " + modelData.memMB.toFixed(1) + " of " + modelData.memLimit + " MB" : "")
                            font.pixelSize: 12; color: Pal.outline
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
                text: "No extensions found."; font.pixelSize: 14; color: Pal.onSurfaceVariant
            }
            Text {
                width: parent.width; wrapMode: Text.WordWrap
                text: "Order matters: lyrics providers are tried from the top down."
                font.pixelSize: 12; color: Pal.outline
                visible: Plugins.list.length > 1
            }
            Item { width: 1; height: 18 }
            Text { text: "Get more extensions"; font.pixelSize: 20; font.weight: Font.DemiBold; color: Pal.onSurface }
            Text {
                width: parent.width; wrapMode: Text.WordWrap
                text: "Extensions are listed in an index you choose. Each download is checked against the checksum in that index before it's unpacked, and nothing runs until you switch it on."
                font.pixelSize: 14; color: Pal.onSurfaceVariant
            }
            Row {
                width: parent.width; spacing: 8
                Rectangle {
                    width: parent.width - browseStore.width - 8; height: 48; radius: 24; color: Pal.surfaceHigh
                    border.width: indexInput.activeFocus ? 2 : 0; border.color: Pal.primary
                    TextInput {
                        id: indexInput
                        anchors.fill: parent; anchors.leftMargin: 20; anchors.rightMargin: 20
                        verticalAlignment: TextInput.AlignVCenter
                        font.pixelSize: 14; color: Pal.onSurface; clip: true; selectByMouse: true
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
                text: Market.message; font.pixelSize: 13; color: Pal.onSurfaceVariant
            }
            Repeater {
                model: Market.items
                delegate: Rectangle {
                    id: sc
                    property bool confirming: false
                    readonly property var local: { var l = Plugins.list; for (var i = 0; i < l.length; ++i) if (l[i].id === modelData.id) return l[i]; return null }
                    readonly property bool installed: local !== null
                    readonly property bool outdated: installed && local.version !== modelData.version
                    width: col.width; height: scol.height + 36; radius: 24; color: Pal.surfaceHigh
                    Rectangle { id: sb; x: 18; y: 18; width: 44; height: 44; radius: 16; color: Pal.secondaryContainer
                                Icon { anchors.centerIn: parent; name: "cloud"; size: 24; color: Pal.onSecondaryContainer } }
                    Column {
                        id: scol
                        anchors.left: sb.right; anchors.leftMargin: 16; anchors.right: sact.left; anchors.rightMargin: 12; y: 18
                        spacing: 6
                        Row {
                            spacing: 8
                            Text { text: modelData.name; font.pixelSize: 16; font.weight: Font.DemiBold; color: Pal.onSurface }
                            Text { anchors.baseline: parent.children[0].baseline; text: modelData.version; font.pixelSize: 13; color: Pal.onSurfaceVariant }
                        }
                        Text { width: parent.width; wrapMode: Text.WordWrap; text: modelData.description || ""; font.pixelSize: 13; color: Pal.onSurfaceVariant }
                        Flow {
                            width: parent.width; spacing: 6
                            Chip { text: "Community" }
                            Repeater { model: modelData.capabilities || []; delegate: Chip { text: modelData } }
                        }
                        Text {
                            width: parent.width; wrapMode: Text.WordWrap
                            visible: (modelData.setup || "") !== ""
                            text: "Setup: " + modelData.setup; font.pixelSize: 12; color: Pal.outline
                        }
                        Text {
                            width: parent.width; wrapMode: Text.WordWrap
                            visible: sc.confirming
                            text: "This extension will run on your computer with your permissions. Only continue if you trust its author."
                            font.pixelSize: 13; color: "#ffb4ab"
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
                text: Plugins.userDir; font.pixelSize: 12; color: Pal.outline
            }
        }
    }
    ScrollIndicator { view: scroller; anchors.top: scroller.top; anchors.bottom: scroller.bottom; height: undefined }
}
