import QtQuick
import QtQuick.Window

// Square cover art with the rounded corners baked in by the image provider.
Item {
    id: root
    property string key: ""
    property string url: ""          // remote art from an extension
    property real radiusPct: 12
    readonly property int px: Math.max(32, Math.ceil(width * Screen.devicePixelRatio / 32) * 32)

    Rectangle {
        anchors.fill: parent
        radius: root.width * root.radiusPct / 100
        color: Pal.surfaceHigh
        visible: img.status !== Image.Ready
        Icon {
            anchors.centerIn: parent
            name: "music_note"
            size: Math.max(16, root.width * 0.38)
            color: Pal.outline
        }
    }
    Image {
        id: img
        anchors.fill: parent
        asynchronous: true
        cache: true
        smooth: true
        sourceSize: Qt.size(root.px, root.px)
        source: root.url !== "" ? root.url : (root.key && root.width > 0 ? "image://cover/" + root.key + "@" + root.px + "@" + root.radiusPct : "")
        opacity: status === Image.Ready ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 140 } }
    }
}
