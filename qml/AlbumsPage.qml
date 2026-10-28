import QtQuick

Item {
    id: root
    SortBar {
        id: bar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        proxy: Library.albums
        sorts: [{ key: "default", label: "Artist, then album" }, { key: "title", label: "Title" }, { key: "year", label: "Year" },
                { key: "added", label: "Recently added" }, { key: "plays", label: "Most played" }]
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
        model: Library.albums
        delegate: MediaCard {
            cardWidth: grid.cellWidth - 4
            title: model.title; subtitle: model.artist; cover: model.cover
            onClicked: Bus.go("album", model.key)
            onPlayClicked: Player.playAlbum(model.key)
            onMenuRequested: (x, y) => Bus.menu("album", { key: model.key, artist: model.artist }, x, y)
        }
    }
    Text {
        anchors.centerIn: grid
        visible: grid.count === 0
        text: "No albums match these filters"
        font.pixelSize: Math.round(15 * Pal.fs); color: Pal.onSurfaceVariant
    }
    ScrollIndicator { view: grid; anchors.top: grid.top; anchors.bottom: grid.bottom; height: undefined }
}
