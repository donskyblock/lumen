import QtQuick

// Pill button: filled (primary) or tonal.
Item {
    id: root
    property string text: ""
    property string icon: ""
    property bool filled: true
    signal clicked()

    implicitHeight: 48
    implicitWidth: row.implicitWidth + 40
    activeFocusOnTab: true
    Keys.onSpacePressed: clicked()
    Keys.onReturnPressed: clicked()

    Rectangle {
        anchors.fill: parent
        radius: height * Pal.pf
        color: root.filled ? Pal.primary : Pal.secondaryContainer
    }
    Rectangle {
        anchors.fill: parent
        radius: height * Pal.pf
        color: root.filled ? Pal.onPrimary : Pal.onSecondaryContainer
        opacity: mouse.pressed ? 0.16 : (mouse.containsMouse ? 0.08 : 0)
        Behavior on opacity { NumberAnimation { duration: Pal.motion ? 100 : 0 } }
    }
    Rectangle {
        anchors.fill: parent
        anchors.margins: -3
        radius: height * Pal.pf
        color: "transparent"
        border.width: 2
        border.color: Pal.primary
        visible: root.activeFocus
    }
    Row {
        id: row
        anchors.centerIn: parent
        spacing: 8
        Icon {
            anchors.verticalCenter: parent.verticalCenter
            visible: root.icon !== ""
            name: root.icon
            size: 22
            color: root.filled ? Pal.onPrimary : Pal.onSecondaryContainer
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            font.pixelSize: Math.round(15 * Pal.fs)
            font.weight: Font.DemiBold
            color: root.filled ? Pal.onPrimary : Pal.onSecondaryContainer
        }
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: { root.forceActiveFocus(); root.clicked() }
    }
}
