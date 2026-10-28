import QtQuick

// Playlist / mix artwork: one cover, or a 2x2 tile of four distinct ones.
Item {
    id: root
    property var covers: []
    property real radiusPct: 14

    Cover {
        anchors.fill: parent
        visible: root.covers.length < 4
        key: root.covers.length > 0 ? root.covers[0] : ""
        radiusPct: root.radiusPct
    }
    Grid {
        anchors.fill: parent
        visible: root.covers.length >= 4
        columns: 2
        spacing: 3
        Repeater {
            model: root.covers.length >= 4 ? 4 : 0
            delegate: Cover {
                width: (root.width - 3) / 2
                height: width
                key: root.covers[index]
                radiusPct: 18
            }
        }
    }
}
