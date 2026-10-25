import QtQuick

// Material 3 switch.
Item {
    id: root
    property bool checked: false
    signal toggled(bool checked)

    implicitWidth: 52
    implicitHeight: 32
    activeFocusOnTab: true
    Accessible.role: Accessible.CheckBox
    Accessible.checked: checked
    Keys.onSpacePressed: root.toggled(!root.checked)

    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: root.checked ? Pal.primary : Pal.surfaceHighest
        border.width: root.checked ? 0 : 2
        border.color: Pal.outline
        Behavior on color { ColorAnimation { duration: 140 } }
    }
    Rectangle {
        anchors.fill: parent
        anchors.margins: -3
        radius: height / 2
        color: "transparent"
        border.width: 2
        border.color: Pal.primary
        visible: root.activeFocus
    }
    Rectangle {
        id: knob
        width: root.checked ? 24 : 16
        height: width
        radius: width / 2
        anchors.verticalCenter: parent.verticalCenter
        x: root.checked ? parent.width - width - 4 : 8
        color: root.checked ? Pal.onPrimary : Pal.outline
        Behavior on x { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
        Behavior on width { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
        Icon {
            anchors.centerIn: parent
            visible: root.checked
            name: "check"; size: 16; color: Pal.primary
        }
    }
    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: { root.forceActiveFocus(); root.toggled(!root.checked) }
    }
}
