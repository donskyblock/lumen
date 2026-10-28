import QtQuick

Rectangle {
    id: root
    property string text: ""
    property bool accent: false
    implicitHeight: 24
    implicitWidth: label.implicitWidth + 20
    radius: height * Pal.pf
    color: accent ? Pal.tertiary : Pal.surfaceHighest

    Text {
        id: label
        anchors.centerIn: parent
        text: root.text
        color: root.accent ? Pal.onTertiary : Pal.onSurfaceVariant
        font.pixelSize: Math.round(12 * Pal.fs)
        font.weight: Font.Medium
    }
}
