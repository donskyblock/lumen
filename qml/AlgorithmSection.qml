import QtQuick

// "Your algorithm": the taste profile Lumen has learned, the knobs that steer it,
// and export / import so it can move to another device.
Column {
    id: root
    spacing: 10
    property string note: ""
    property var dialog: null

    function pick(save) {
        try {
            var d = Qt.createQmlObject('import QtQuick.Dialogs\nFileDialog { nameFilters: ["Lumen algorithm (*.json)"] ' +
                                       (save ? 'fileMode: FileDialog.SaveFile; ' : 'fileMode: FileDialog.OpenFile') + ' }', root)
            d.accepted.connect(function () {
                var f = d.selectedFile.toString()
                if (save) {
                    if (!/\.json$/i.test(f)) f += ".json"
                    root.note = Taste.exportTo(f) || "Saved your algorithm."
                } else Taste.importFrom(f)
            })
            d.open()
        } catch (e) {
            root.note = "The file picker isn't available here. Type a path below instead."
        }
    }

    Text { text: "Your algorithm"; font.pixelSize: Math.round(20 * Pal.fs); font.weight: Font.DemiBold; color: Pal.onSurface }
    Text {
        width: parent.width; wrapMode: Text.WordWrap
        text: "Lumen learns from what you play, like and skip, and from artists you add to your library. It builds your radio, Discover and mixes from that. The knobs below steer it. Export saves all of it, plus your playlists and saved artists, into one file you can import on another device."
        font.pixelSize: Math.round(14 * Pal.fs); color: Pal.onSurfaceVariant
    }

    // what it has learned
    Text {
        visible: Taste.profile.length > 0
        text: "What it has learned about you"; font.pixelSize: Math.round(13 * Pal.fs); font.weight: Font.DemiBold; color: Pal.onSurfaceVariant
    }
    Flow {
        width: parent.width; spacing: 6
        visible: Taste.profile.length > 0
        Repeater {
            model: Taste.profile
            delegate: Rectangle {
                height: 30; width: nm.implicitWidth + 24; radius: 15 * Pal.rs
                color: Qt.rgba(Pal.primary.r, Pal.primary.g, Pal.primary.b, 0.10 + 0.35 * modelData.affinity)
                Text { id: nm; anchors.centerIn: parent; text: modelData.name; font.pixelSize: Math.round(13 * Pal.fs); color: Pal.onSurface }
            }
        }
    }
    Text {
        visible: Taste.profile.length === 0
        width: parent.width; wrapMode: Text.WordWrap
        text: "Nothing yet. Play some songs or add an artist to your library and this fills in."
        font.pixelSize: Math.round(13 * Pal.fs); color: Pal.outline
    }

    Repeater {
        model: Taste.paramInfo
        delegate: Item {
            width: root.width; height: 66
            Text { x: 0; text: modelData.label; font.pixelSize: Math.round(15 * Pal.fs); font.weight: Font.Medium; color: Pal.onSurface }
            Text { anchors.right: parent.right; text: modelData.key === "recency" ? Math.round(modelData.value) + " days" : modelData.value.toFixed(2)
                   font.pixelSize: Math.round(13 * Pal.fs); color: Pal.onSurfaceVariant }
            Text { y: 20; width: parent.width; elide: Text.ElideRight; text: modelData.help; font.pixelSize: Math.round(12 * Pal.fs); color: Pal.outline }
            Sld {
                y: 38; width: parent.width
                from: modelData.min; to: modelData.max; value: modelData.value; step: (modelData.max - modelData.min) / 20
                onMoved: (v) => Taste.setParam(modelData.key, v)
            }
        }
    }

    Row {
        spacing: 10
        Pill { icon: "content_copy"; text: "Export…"; onClicked: root.pick(true) }
        Pill { icon: "folder_open"; text: "Import…"; filled: false; enabled: Taste.busy === ""; onClicked: root.pick(false) }
        Pill { icon: "refresh"; text: "Reset knobs"; filled: false; onClicked: Taste.resetParams() }
    }
    Row {
        width: parent.width; spacing: 8
        Rectangle {
            width: parent.width - goBtn.width - 8; height: 44; radius: 22 * Pal.rs; color: Pal.surfaceHigh
            border.width: pathIn.activeFocus ? 2 : 0; border.color: Pal.primary
            TextInput {
                id: pathIn
                anchors.fill: parent; anchors.leftMargin: 18; anchors.rightMargin: 18
                verticalAlignment: TextInput.AlignVCenter; clip: true; selectByMouse: true
                font.pixelSize: Math.round(14 * Pal.fs); color: Pal.onSurface
                Accessible.name: "Algorithm file path"
            }
            Text { anchors.fill: pathIn; verticalAlignment: Text.AlignVCenter; visible: pathIn.text === ""
                   text: "…or a file path, e.g. ~/lumen-algorithm.json"; font.pixelSize: Math.round(14 * Pal.fs); color: Pal.onSurfaceVariant }
        }
        Pill { id: goBtn; implicitHeight: 44; text: "Export here"; filled: false
               onClicked: { root.note = Taste.exportTo(pathIn.text) || "Saved your algorithm." } }
    }
    Text {
        width: parent.width; wrapMode: Text.WordWrap
        visible: Taste.busy !== "" || root.note !== ""
        text: Taste.busy !== "" ? Taste.busy : root.note
        font.pixelSize: Math.round(13 * Pal.fs); color: Pal.onSurfaceVariant
    }
    Row {
        width: parent.width; spacing: 12
        Column {
            width: parent.width - 64; spacing: 2
            Text { text: "Learn music in the background"; font.pixelSize: Math.round(14 * Pal.fs); font.weight: Font.Medium; color: Pal.onSurface }
            Text {
                width: parent.width; wrapMode: Text.WordWrap
                text: "Slowly looks up top songs and related artists for the artists you listen to and search for, and keeps a small local index (" + SongIndex.stats.songs + " songs, " + SongIndex.stats.artists + " artists so far) that Up Next draws from."
                font.pixelSize: Math.round(12 * Pal.fs); color: Pal.onSurfaceVariant
            }
        }
        Switch { anchors.verticalCenter: parent.verticalCenter; checked: SongIndex.passive; onToggled: (on) => SongIndex.passive = on }
    }
    Connections {
        target: Taste
        function onImportFinished(summary) { root.note = summary }
        function onImportFailed(why) { root.note = why }
    }
}
