import QtQuick

// Sort + quality filters over a ViewProxy.
Item {
    id: root
    property var proxy
    property var sorts: []                // [{key, label}]
    property bool codecFilters: true
    property bool quality: true
    property bool open: false
    implicitHeight: 52
    z: 5

    readonly property string sortLabel: {
        for (var i = 0; i < sorts.length; ++i) if (sorts[i].key === proxy.sortKey) return sorts[i].label
        return sorts.length ? sorts[0].label : ""
    }

    Row {
        anchors.left: parent.left
        anchors.leftMargin: 24
        anchors.verticalCenter: parent.verticalCenter
        spacing: 8

        // sort picker
        Item {
            id: picker
            width: pr.implicitWidth + 28; height: 36
            activeFocusOnTab: true
            Keys.onSpacePressed: root.open = !root.open
            Rectangle { anchors.fill: parent; radius: height * Pal.pf; color: "transparent"; border.width: picker.activeFocus ? 2 : 1; border.color: picker.activeFocus ? Pal.primary : Pal.outlineVariant }
            Rectangle { anchors.fill: parent; radius: height * Pal.pf; color: Pal.onSurface; opacity: pm.containsMouse ? 0.06 : 0 }
            Row {
                id: pr
                anchors.centerIn: parent
                spacing: 6
                Icon { anchors.verticalCenter: parent.verticalCenter; name: "sort"; size: 18; color: Pal.onSurfaceVariant }
                Text { anchors.verticalCenter: parent.verticalCenter; text: root.sortLabel; font.pixelSize: Math.round(13 * Pal.fs); font.weight: Font.Medium; color: Pal.onSurface }
                Icon { anchors.verticalCenter: parent.verticalCenter; name: "arrow_drop_down"; size: 18; color: Pal.onSurfaceVariant }
            }
            MouseArea { id: pm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.open = !root.open }
        }
        IconButton {
            width: 36; height: 36
            icon: root.proxy.descending ? "arrow_downward" : "arrow_upward"; iconSize: 18
            visible: root.proxy.sortKey !== "default"
            onClicked: root.proxy.descending = !root.proxy.descending
        }
        Rectangle { visible: root.quality; width: 1; height: 22; anchors.verticalCenter: parent.verticalCenter; color: Pal.outlineVariant }
        FilterChip { visible: root.quality; text: "Lossless"; checked: root.proxy.losslessOnly; onClicked: root.proxy.losslessOnly = !root.proxy.losslessOnly }
        FilterChip { visible: root.quality; text: "Hi-Res"; checked: root.proxy.hiResOnly; onClicked: root.proxy.hiResOnly = !root.proxy.hiResOnly }
        Repeater {
            model: root.quality && root.codecFilters && root.width > 760 ? Library.codecs : []
            delegate: FilterChip { text: modelData; checked: root.proxy.codec === modelData
                                   onClicked: root.proxy.codec = root.proxy.codec === modelData ? "" : modelData }
        }
    }

    // dropdown
    MouseArea { anchors.fill: parent; anchors.topMargin: 0; visible: root.open; onClicked: root.open = false; z: -1 }
    Rectangle {
        visible: root.open
        x: 24; y: 46
        width: 220; height: dcol.height + 16
        radius: 18 * Pal.rs
        color: Pal.surfaceHighest
        border.width: 1; border.color: Pal.outlineVariant
        Column {
            id: dcol
            x: 8; y: 8; width: parent.width - 16
            Repeater {
                model: root.sorts
                delegate: Item {
                    width: dcol.width; height: 40
                    Rectangle { anchors.fill: parent; radius: 12 * Pal.rs; color: Pal.onSurface; opacity: dm.containsMouse ? 0.09 : 0 }
                    Icon { x: 10; anchors.verticalCenter: parent.verticalCenter; name: "check"; size: 18; color: Pal.primary
                           visible: modelData.key === root.proxy.sortKey }
                    Text { x: 38; anchors.verticalCenter: parent.verticalCenter; text: modelData.label; font.pixelSize: Math.round(14 * Pal.fs); color: Pal.onSurface }
                    MouseArea {
                        id: dm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            root.proxy.sortKey = modelData.key
                            // sensible direction per key: numbers/dates newest-first, text A-Z
                            root.proxy.descending = ["plays", "added", "year", "count", "durationMs"].indexOf(modelData.key) >= 0 && modelData.key !== "durationMs"
                            root.open = false
                        }
                    }
                }
            }
        }
    }
}
