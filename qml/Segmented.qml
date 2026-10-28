import QtQuick

// Single-choice segmented control.  options: [{value, label}]
Item {
    id: root
    property var options: []
    property var value
    signal picked(var value)

    implicitHeight: 40
    implicitWidth: row.implicitWidth

    Rectangle {
        anchors.fill: parent
        radius: height * Pal.pf
        color: "transparent"
        border.width: 1
        border.color: Pal.outline
    }
    Row {
        id: row
        height: parent.height
        Repeater {
            model: root.options
            delegate: Item {
                id: seg
                readonly property bool on: modelData.value === root.value
                width: lab.implicitWidth + 32
                height: root.height
                activeFocusOnTab: true
                Keys.onSpacePressed: root.picked(modelData.value)
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 2
                    radius: height * Pal.pf
                    color: seg.on ? Pal.secondaryContainer : (m.containsMouse ? Pal.surfaceHigh : "transparent")
                    border.width: seg.activeFocus ? 2 : 0
                    border.color: Pal.primary
                }
                Text {
                    id: lab
                    anchors.centerIn: parent
                    text: modelData.label
                    font.pixelSize: Math.round(13 * Pal.fs)
                    font.weight: seg.on ? Font.DemiBold : Font.Medium
                    color: seg.on ? Pal.onSecondaryContainer : Pal.onSurface
                }
                MouseArea { id: m; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.picked(modelData.value) }
            }
        }
    }
}
