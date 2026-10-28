import QtQuick

Item {
    id: root
    SortBar {
        id: bar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        proxy: Library.songs
        sorts: [{ key: "default", label: "Artist, then album" }, { key: "title", label: "Title" }, { key: "artist", label: "Artist" },
                { key: "album", label: "Album" }, { key: "year", label: "Year" }, { key: "added", label: "Recently added" },
                { key: "plays", label: "Most played" }, { key: "durationMs", label: "Length" }]
    }
    Text {
        anchors.right: parent.right; anchors.rightMargin: 28
        anchors.verticalCenter: bar.verticalCenter
        text: Library.songs.count + (Library.songs.count === 1 ? " song" : " songs")
        font.pixelSize: Math.round(13 * Pal.fs); color: Pal.onSurfaceVariant
    }
    ListView {
        id: list
        anchors.top: bar.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        clip: true
        cacheBuffer: 400
        boundsBehavior: Flickable.StopAtBounds
        model: Library.songs
        delegate: TrackRow {
            width: list.width
            index: model.index
            path: model.path; title: model.title; artist: model.artist; album: model.album
            cover: model.cover; quality: model.quality; lossless: model.lossless; durationMs: model.durationMs
            showAlbum: true
            onActivated: Player.playSong(model.index)
        }
    }
    Text {
        anchors.centerIn: list
        visible: list.count === 0
        text: "No songs match these filters"
        font.pixelSize: Math.round(15 * Pal.fs); color: Pal.onSurfaceVariant
    }
    ScrollIndicator { view: list; anchors.top: list.top; anchors.bottom: list.bottom; height: undefined }
}
