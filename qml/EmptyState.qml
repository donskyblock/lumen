import QtQuick

Item {
    id: root
    property bool scanning: false
    property string msg: ""
    signal openSettings()

    Column {
        anchors.centerIn: parent
        width: Math.min(440, parent.width - 48)
        spacing: 14

        Rectangle {
            width: 72; height: 72; radius: 26 * Pal.rs
            color: Pal.primaryContainer
            anchors.horizontalCenter: parent.horizontalCenter
            Icon { anchors.centerIn: parent; name: root.scanning ? "refresh" : "folder"; size: 34; color: Pal.onPrimaryContainer }
        }
        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: root.scanning ? "Reading your library\u2026" : "Add your music"
            font.pixelSize: Math.round(24 * Pal.fs)
            font.weight: Font.DemiBold
            color: Pal.onSurface
        }
        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: root.scanning ? Library.scanned + " tracks found so far."
                                : "Choose the folder that holds your FLAC, WAV, MP3 or other audio files. Subfolders are included."
            font.pixelSize: Math.round(14 * Pal.fs)
            color: Pal.onSurfaceVariant
        }
        Rectangle {
            visible: !root.scanning
            width: parent.width; height: 48; radius: 24 * Pal.rs
            color: Pal.surfaceHigh
            border.width: path.activeFocus ? 2 : 0
            border.color: Pal.primary
            TextInput {
                id: path
                anchors.fill: parent
                anchors.leftMargin: 20; anchors.rightMargin: 20
                verticalAlignment: TextInput.AlignVCenter
                font.pixelSize: Math.round(15 * Pal.fs)
                color: Pal.onSurface
                clip: true
                text: "~/Music"
                selectByMouse: true
                Accessible.name: "Music folder"
                onAccepted: root.msg = Library.addFolder(text)
            }
        }
        Pill {
            visible: !root.scanning
            anchors.horizontalCenter: parent.horizontalCenter
            icon: "folder"; text: "Scan folder"
            onClicked: root.msg = Library.addFolder(path.text)
        }
        Text {
            width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
            visible: root.msg !== ""
            text: root.msg; font.pixelSize: Math.round(13 * Pal.fs); color: Pal.error
        }
        Text {
            width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
            visible: !root.scanning
            text: "You can also drop a folder onto this window. No music yet? Search for any artist above to browse and play from the online catalog."
            font.pixelSize: Math.round(13 * Pal.fs); color: Pal.outline
        }
    }
}
