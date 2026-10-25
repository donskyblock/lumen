import QtQuick

Rectangle {
    id: root
    property alias text: input.text
    property string placeholder: "Search"

    implicitHeight: 48
    radius: height / 2
    color: Pal.surfaceHigh
    border.width: input.activeFocus ? 2 : 0
    border.color: Pal.primary

    Icon {
        id: lens
        anchors.left: parent.left
        anchors.leftMargin: 16
        anchors.verticalCenter: parent.verticalCenter
        name: "search"
        color: Pal.onSurfaceVariant
    }
    TextInput {
        id: input
        anchors.left: lens.right
        anchors.leftMargin: 12
        anchors.right: clear.left
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        font.pixelSize: 15
        color: Pal.onSurface
        selectionColor: Pal.primaryContainer
        selectedTextColor: Pal.onPrimaryContainer
        clip: true
        Accessible.name: root.placeholder
        Keys.onEscapePressed: { text = ""; focus = false }
    }
    Text {
        anchors.fill: input
        verticalAlignment: Text.AlignVCenter
        text: root.placeholder
        color: Pal.onSurfaceVariant
        font.pixelSize: 15
        visible: input.text.length === 0
    }
    IconButton {
        id: clear
        anchors.right: parent.right
        anchors.rightMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        width: 40; height: 40
        icon: "close"
        iconSize: 20
        visible: input.text.length > 0
        onClicked: input.text = ""
    }
}
