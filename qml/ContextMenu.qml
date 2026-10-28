import QtQuick

// Pop-up list.  items: [{id, label, icon, danger, sep}]
Item {
    id: root
    property var items: []
    property var ctx: ({})
    signal chosen(string id)
    visible: false
    z: 200

    function popup(px, py) {
        card.x = Math.max(8, Math.min(px, root.width - card.width - 8))
        card.y = Math.max(8, Math.min(py, root.height - card.height - 8))
        visible = true
        card.forceActiveFocus()
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onPressed: root.visible = false
    }
    Rectangle {
        id: card
        width: 244
        height: col.height + 16
        radius: 20 * Pal.rs
        color: Pal.surfaceHighest
        border.width: 1
        border.color: Pal.outlineVariant
        focus: true
        Keys.onEscapePressed: root.visible = false

        Column {
            id: col
            x: 8; y: 8
            width: parent.width - 16
            Repeater {
                model: root.items
                delegate: Item {
                    width: col.width
                    height: modelData.sep ? 9 : 42
                    Rectangle { visible: !!modelData.sep; anchors.centerIn: parent; width: parent.width - 16; height: 1; color: Pal.outlineVariant }
                    Rectangle {
                        visible: !modelData.sep
                        anchors.fill: parent
                        radius: 14 * Pal.rs
                        color: Pal.onSurface
                        opacity: im.containsMouse ? 0.09 : 0
                    }
                    Row {
                        visible: !modelData.sep
                        anchors.verticalCenter: parent.verticalCenter
                        x: 12
                        spacing: 12
                        Icon { anchors.verticalCenter: parent.verticalCenter; name: modelData.icon || ""; size: 20
                               color: modelData.danger ? Pal.error : Pal.onSurfaceVariant; visible: (modelData.icon || "") !== "" }
                        Text { anchors.verticalCenter: parent.verticalCenter; text: modelData.label || ""; font.pixelSize: Math.round(14 * Pal.fs)
                               color: modelData.danger ? Pal.error : Pal.onSurface }
                    }
                    MouseArea {
                        id: im
                        enabled: !modelData.sep
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: { root.visible = false; root.chosen(modelData.id) }
                    }
                }
            }
        }
    }
}
