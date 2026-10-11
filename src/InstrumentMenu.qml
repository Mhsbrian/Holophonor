import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Top left: the instrument's name, set like a title. Click it, or press / or
// Ctrl+K, and a card unfolds to search the catalog.
Item {
    id: root
    property QtObject style
    readonly property bool opened: popup.opened
    signal closed

    function open() {
        popup.open();
    }

    implicitWidth: header.implicitWidth
    implicitHeight: header.implicitHeight

    Column {
        id: header
        spacing: 3

        Text {
            text: backend.instrumentFamily.toUpperCase()
            font.family: root.style.mono
            font.pixelSize: 10
            font.letterSpacing: 2.6
            color: root.style.ink
            opacity: 0.42
        }
        Row {
            spacing: 12
            Text {
                id: title
                text: backend.instrumentName
                font.family: root.style.serif
                font.weight: Font.Light
                font.pixelSize: 32
                color: root.style.ink
            }
            Chevron {
                anchors.verticalCenter: title.verticalCenter
                anchors.verticalCenterOffset: 3
                color: root.style.ink
                open: popup.opened
                opacity: hover.containsMouse || popup.opened ? 0.7 : 0.3
            }
        }
        // A hairline that draws itself under the name, like a pen stroke.
        Rectangle {
            width: hover.containsMouse || popup.opened ? title.width : 0
            height: 1
            color: root.style.ink
            opacity: 0.32
            Behavior on width { NumberAnimation { duration: 280; easing.type: Easing.OutCubic } }
        }
    }

    MouseArea {
        id: hover
        anchors.fill: header
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: popup.opened ? popup.close() : popup.open()
    }

    Popup {
        id: popup
        y: header.height + 12
        width: 380
        // Fits its results, up to the room the window has.
        height: Math.min(topPadding + bottomPadding + search.implicitHeight + hint.implicitHeight + 24
                         + Math.max(list.contentHeight, 60), 560, root.Window.height - y - root.y - 90)
        padding: 18
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
        background: Card { style: root.style }

        enter: Transition {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 160 }
            NumberAnimation { property: "y"; from: header.height + 2; to: header.height + 12; duration: 240; easing.type: Easing.OutCubic }
        }
        exit: Transition {
            NumberAnimation { property: "opacity"; to: 0; duration: 120 }
        }

        onOpened: {
            search.text = "";
            search.forceActiveFocus();
            list.currentIndex = Math.max(0, backend.instruments.rowOfPreset(backend.instrument));
            list.positionViewAtIndex(list.currentIndex, ListView.Center);
        }
        onClosed: root.closed()

        function choose(row) {
            const preset = backend.instruments.presetAt(row);
            if (preset >= 0)
                backend.instrument = preset;
            popup.close();
        }

        contentItem: ColumnLayout {
            spacing: 12

            TextField {
                id: search
                Layout.fillWidth: true
                placeholderText: "Search instruments"
                placeholderTextColor: Qt.rgba(root.style.ink.r, root.style.ink.g, root.style.ink.b, 0.35)
                color: root.style.ink
                selectionColor: root.style.accent
                selectedTextColor: theme.accentForeground
                font.family: root.style.serif
                font.italic: true
                font.pixelSize: 19
                leftPadding: 0
                rightPadding: 0
                topPadding: 4
                bottomPadding: 8
                background: Rectangle {
                    y: parent.height - 1
                    width: parent.width
                    height: 1
                    color: root.style.ink
                    opacity: search.activeFocus ? 0.35 : 0.15
                }
                onTextChanged: {
                    backend.instruments.query = text;
                    list.currentIndex = 0;
                }
                Keys.onDownPressed: list.incrementCurrentIndex()
                Keys.onUpPressed: list.decrementCurrentIndex()
                Keys.onReturnPressed: popup.choose(list.currentIndex)
                Keys.onEnterPressed: popup.choose(list.currentIndex)
            }

            ListView {
                id: list
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: backend.instruments
                boundsBehavior: Flickable.StopAtBounds
                highlightMoveDuration: 90
                section.property: search.text.length > 0 ? "" : "family"
                section.delegate: Text {
                    required property string section
                    text: section.toUpperCase()
                    topPadding: 12
                    bottomPadding: 4
                    font.family: root.style.mono
                    font.pixelSize: 10
                    font.letterSpacing: 2.4
                    color: root.style.ink
                    opacity: 0.38
                }
                highlight: Rectangle {
                    radius: 4
                    color: root.style.ink
                    opacity: 0.06
                }
                delegate: Item {
                    id: row
                    required property int index
                    required property string name
                    required property string family
                    required property int preset
                    width: ListView.view.width
                    height: 34

                    Rectangle {
                        x: 8
                        anchors.verticalCenter: parent.verticalCenter
                        width: 5
                        height: 5
                        radius: 2.5
                        color: root.style.accent
                        visible: row.preset === backend.instrument
                    }
                    Text {
                        x: 22
                        anchors.verticalCenter: parent.verticalCenter
                        text: row.name
                        font.family: root.style.serif
                        font.pixelSize: 18
                        font.weight: row.ListView.isCurrentItem ? Font.Normal : Font.Light
                        color: root.style.ink
                    }
                    Text {
                        anchors.right: parent.right
                        anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        visible: search.text.length > 0
                        text: row.family.toLowerCase()
                        font.family: root.style.mono
                        font.pixelSize: 10
                        color: root.style.ink
                        opacity: 0.4
                    }
                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onEntered: list.currentIndex = row.index
                        onClicked: popup.choose(row.index)
                    }
                }

                Text {
                    anchors.centerIn: parent
                    visible: list.count === 0
                    text: "Nothing by that name — yet."
                    font.family: root.style.serif
                    font.italic: true
                    font.pixelSize: 16
                    color: root.style.ink
                    opacity: 0.5
                }
            }

            Text {
                id: hint
                text: "↑↓ choose    ⏎ play    esc close"
                font.family: root.style.mono
                font.pixelSize: 10
                color: root.style.ink
                opacity: 0.32
            }
        }
    }
}
