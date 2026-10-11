mod app;
mod visuals;

fn main() -> eframe::Result {
    let args: Vec<String> = std::env::args().skip(1).collect();
    if args.iter().any(|arg| arg == "--help") {
        println!(
            "Holophonor — sound, made visible\n\ncargo run --release\n  --demo          Start the preview melody\n  --silent        Open the canvas without an audio stream\n  --capture FILE  Save a native window PNG and exit (visual verification)\n  --settings      Open connection settings\n\nSpace: preview · Esc: silence · F: freeze · C: new canvas · ?: help"
        );
        return Ok(());
    }
    let capture = args
        .iter()
        .position(|arg| arg == "--capture")
        .and_then(|i| args.get(i + 1))
        .cloned();
    let demo = args.iter().any(|arg| arg == "--demo");
    let silent = args.iter().any(|arg| arg == "--silent");
    let settings = args.iter().any(|arg| arg == "--settings");
    let options = eframe::NativeOptions {
        viewport: eframe::egui::ViewportBuilder::default()
            .with_title("Holophonor")
            .with_app_id("holophonor")
            .with_inner_size([1280.0, 820.0])
            .with_min_inner_size([760.0, 560.0]),
        renderer: eframe::Renderer::Glow,
        ..Default::default()
    };
    eframe::run_native(
        "Holophonor",
        options,
        Box::new(move |cc| {
            Ok(Box::new(app::Holophonor::new(
                cc, demo, silent, capture, settings,
            )))
        }),
    )
}
