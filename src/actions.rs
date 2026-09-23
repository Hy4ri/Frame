use gpui::actions;

actions!(
    frame,
    [
        NextImage,
        PrevImage,
        FirstImage,
        LastImage,
        ToggleFullscreen,
        ZoomIn,
        ZoomOut,
        ZoomFit,
        ZoomOriginal,
        RotateCW,
        RotateCCW,
        DeleteImage,
        RenameImage,
        ShowInfo,
        ShowHelp,
        OpenSearch,
        Quit,
        CloseOverlay,
    ]
);
