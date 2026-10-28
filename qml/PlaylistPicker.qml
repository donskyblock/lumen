import QtQuick

// "Add to playlist" sheet.
Item {
    id: root
    property var paths: []
    signal newPlaylist(var paths)
    visible: false
    z: 250

    function open(p) { paths = p; visible = true }

    Rectangle { anchors.fill: parent; color: "#000000"; opacity: 0.5 }
    MouseArea { anchors.fill: parent; onClicked: root.visible = false }
    Rectangle {
        anchors.centerIn: parent
        width: Math.min(440, root.width - 48)
        height: Math.min(root.height - 80, head.height + list.contentHeight + 40 + foot.height)
        radius: 28 * Pal.rs
        color: Pal.surfaceHighest
        MouseArea { anchors.fill: parent }
        Text { id: head; x: 24; y: 22; text: "Add to playlist"; font.pixelSize: Math.round(22 * Pal.fs); font.weight: Font.DemiBold; color: Pal.onSurface }
        ListView {
            id: list
            anchors.top: head.bottom; anchors.topMargin: 14
            anchors.bottom: foot.top; anchors.bottomMargin: 4
            anchors.left: parent.left; anchors.right: parent.right
            clip: true
            model: Playlists.list
            delegate: Item {
                width: list.width; height: 60
                Rectangle { anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10; radius: 16 * Pal.rs
                            color: Pal.onSurface; opacity: pm.containsMouse ? 0.08 : 0 }
                Mosaic { id: pc; x: 18; anchors.verticalCenter: parent.verticalCenter; width: 44; height: 44; covers: modelData.covers; radiusPct: 18 }
                Column {
                    anchors.left: pc.right; anchors.leftMargin: 14; anchors.verticalCenter: parent.verticalCenter
                    Text { text: modelData.name; font.pixelSize: Math.round(15 * Pal.fs); font.weight: Font.Medium; color: Pal.onSurface }
                    Text { text: modelData.count + (modelData.count === 1 ? " song" : " songs"); font.pixelSize: Math.round(12 * Pal.fs); color: Pal.onSurfaceVariant }
                }
                MouseArea {
                    id: pm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        var n = Playlists.add(modelData.id, root.paths)
                        root.visible = false
                        Bus.toast(n > 0 ? "Added to " + modelData.name : "Already in " + modelData.name)
                    }
                }
            }
        }
        Item {
            id: foot
            anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right
            height: 76
            Pill { anchors.centerIn: parent; icon: "add"; text: "New playlist"; filled: false
                   onClicked: { root.visible = false; root.newPlaylist(root.paths) } }
        }
    }
}
