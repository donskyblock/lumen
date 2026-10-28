import QtQuick

Item {
    id: root
    SortBar {
        id: bar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        proxy: Library.artists
        quality: false
        sorts: [{ key: "default", label: "Name" }, { key: "plays", label: "Most played" }, { key: "count", label: "Most songs" },
                { key: "albumCount", label: "Most albums" }]
    }
    GridView {
        id: grid
        anchors.top: bar.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        readonly property int cols: Math.max(2, Math.floor(width / (Pal.cardW + 6)))
        cellWidth: Math.floor(width / cols)
        cellHeight: cellWidth + Math.round(62 * Pal.fs)
        clip: true
        cacheBuffer: 300
        boundsBehavior: Flickable.StopAtBounds
        model: Library.artists
        delegate: MediaCard {
            cardWidth: grid.cellWidth - 4
            circle: true
            title: model.name
            subtitle: model.count + (model.count === 1 ? " song" : " songs")
            cover: model.cover
            onClicked: Bus.go("artist", model.name)
            onPlayClicked: Player.playPaths(Library.artistPaths(model.name), 0)
            onMenuRequested: (x, y) => Bus.menu("artist", { name: model.name }, x, y)
        }
    }
    ScrollIndicator { view: grid; anchors.top: grid.top; anchors.bottom: grid.bottom; height: undefined }
}
