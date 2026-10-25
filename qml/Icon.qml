import QtQuick
import QtQuick.Window

// Material icon rendered by the C++ icon provider (see src/Icons.cpp).
Image {
    id: root
    property string name: ""
    property color color: Pal.onSurface
    property real size: 24
    readonly property int px: Math.round(size * Screen.devicePixelRatio)

    width: size
    height: size
    source: name ? "image://icon/" + name + "@" + px + "@" + String(color).replace("#", "") : ""
    cache: true
    smooth: true
}
