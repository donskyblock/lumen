import QtQuick

// Selectable chip (filters, sort options).
Item {
    id: root
    property string text: ""
    property string icon: ""
    property bool checked: false
    signal clicked()

    implicitHeight: 36
    implicitWidth: row.implicitWidth + 28
    activeFocusOnTab: true
    Keys.onSpacePressed: clicked()
    Keys.onReturnPressed: clicked()
    Accessible.role: Accessible.CheckBox
    Accessible.checked: checked
    Accessible.name: text

    Rectangle {
        anchors.fill: parent
        radius: height * Pal.pf
        color: root.checked ? Pal.secondaryContainer : "transparent"
        border.width: root.checked ? 0 : 1
        border.color: Pal.outlineVariant
        Behavior on color { ColorAnimation { duration: Pal.motion ? 120 : 0 } }
    }
    Rectangle {
        anchors.fill: parent
        radius: height * Pal.pf
        color: Pal.onSurface
        opacity: mouse.pressed ? 0.12 : (mouse.containsMouse ? 0.06 : 0)
    }
    Rectangle {
        anchors.fill: parent
        radius: height * Pal.pf
        color: "transparent"
        border.width: 2
        border.color: Pal.primary
        visible: root.activeFocus
    }
    Row {
        id: row
        anchors.centerIn: parent
        spacing: 6
        Icon {
            anchors.verticalCenter: parent.verticalCenter
            visible: root.icon !== "" || root.checked
            name: root.icon !== "" ? root.icon : "check"
            size: 18
            color: root.checked ? Pal.onSecondaryContainer : Pal.onSurfaceVariant
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            font.pixelSize: Math.round(13 * Pal.fs)
            font.weight: Font.Medium
            color: root.checked ? Pal.onSecondaryContainer : Pal.onSurface
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
