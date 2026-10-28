import QtQuick

// Modal text prompt (playlist / profile names).
Item {
    id: root
    property string title: ""
    property string action: ""
    property var arg
    signal accepted(string action, string text, var arg)
    visible: false
    z: 300

    function ask(t, initial, act, a) {
        title = t; action = act; arg = a
        input.text = initial
        visible = true
        input.forceActiveFocus()
        input.selectAll()
    }

    Rectangle { anchors.fill: parent; color: "#000000"; opacity: 0.5 }
    MouseArea { anchors.fill: parent; onClicked: root.visible = false }
    Rectangle {
        anchors.centerIn: parent
        width: Math.min(420, root.width - 48)
        height: col.height + 48
        radius: 28 * Pal.rs
        color: Pal.surfaceHighest
        MouseArea { anchors.fill: parent }
        Column {
            id: col
            x: 24; y: 24
            width: parent.width - 48
            spacing: 18
            Text { text: root.title; font.pixelSize: Math.round(22 * Pal.fs); font.weight: Font.DemiBold; color: Pal.onSurface }
            Rectangle {
                width: parent.width; height: 52; radius: 14 * Pal.rs; color: Pal.surfaceHigh
                border.width: 2; border.color: Pal.primary
                TextInput {
                    id: input
                    anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 16
                    verticalAlignment: TextInput.AlignVCenter
                    font.pixelSize: Math.round(16 * Pal.fs); color: Pal.onSurface; clip: true; selectByMouse: true
                    Accessible.name: root.title
                    onAccepted: { root.visible = false; root.accepted(root.action, text, root.arg) }
                    Keys.onEscapePressed: root.visible = false
                }
            }
            Row {
                spacing: 10
                anchors.right: parent.right
                Pill { text: "Cancel"; filled: false; onClicked: root.visible = false }
                Pill { text: "Save"; onClicked: { root.visible = false; root.accepted(root.action, input.text, root.arg) } }
            }
        }
    }
}
