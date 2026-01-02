package ui

// StylesCSS contains the custom CSS styling for Frame
const StylesCSS = `
/* Main window styling */
window {
    background-color: #1a1a1a;
}

/* Header bar - minimal and dark */
headerbar {
    background-color: #242424;
    border-bottom: 1px solid #333;
    min-height: 38px;
    padding: 0 6px;
}

headerbar button {
    background: transparent;
    border: none;
    border-radius: 6px;
    padding: 6px 8px;
    margin: 2px;
    color: #e0e0e0;
}

headerbar button:hover {
    background-color: rgba(255, 255, 255, 0.1);
}

headerbar button:active {
    background-color: rgba(255, 255, 255, 0.15);
}

/* Title in header */
headerbar .title {
    font-weight: 500;
    color: #ffffff;
}

/* Scrolled window / viewer area */
scrolledwindow {
    background-color: #1a1a1a;
}

/* Picture widget */
picture {
    background-color: transparent;
}

/* Dialogs */
dialog {
    background-color: #2a2a2a;
    border-radius: 12px;
}

messagedialog {
    background-color: #2a2a2a;
}

messagedialog .message-area {
    padding: 16px;
}

messagedialog label {
    color: #e0e0e0;
}

/* Entry fields in dialogs */
entry {
    background-color: #1a1a1a;
    border: 1px solid #444;
    border-radius: 6px;
    padding: 8px 12px;
    color: #ffffff;
    caret-color: #ffffff;
}

entry:focus {
    border-color: #666;
    outline: none;
}

/* Buttons in dialogs */
button {
    background-color: #333;
    border: none;
    border-radius: 6px;
    padding: 8px 16px;
    color: #e0e0e0;
    font-weight: 500;
}

button:hover {
    background-color: #444;
}

button:active {
    background-color: #555;
}

button.suggested-action {
    background-color: #3584e4;
    color: #ffffff;
}

button.suggested-action:hover {
    background-color: #4a9cf4;
}

button.destructive-action {
    background-color: #c01c28;
    color: #ffffff;
}

button.destructive-action:hover {
    background-color: #e01b24;
}

/* Scrollbars */
scrollbar {
    background-color: transparent;
}

scrollbar slider {
    background-color: rgba(255, 255, 255, 0.2);
    border-radius: 6px;
    min-width: 8px;
    min-height: 8px;
}

scrollbar slider:hover {
    background-color: rgba(255, 255, 255, 0.3);
}

scrollbar slider:active {
    background-color: rgba(255, 255, 255, 0.4);
}
`
