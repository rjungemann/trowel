cask "trowel" do
  version "0.1.1"
  sha256 "6737cc322312c431d5ed8ea1546b777d6a98a488b512f2e7aa8a0509275481f5"

  url "https://github.com/rjungemann/trowel/releases/download/v#{version}/Trowel-#{version}.zip"
  name "Trowel"
  desc "IDE for the Turmeric programming language"
  homepage "https://github.com/rjungemann/trowel"

  depends_on macos: :monterey

  app "Trowel.app"
  binary "#{appdir}/Trowel.app/Contents/Resources/trowel"

  zap trash: [
    "~/Library/Preferences/com.turmeric-lang.TrowelEditor.plist",
    "~/Library/Application Support/Trowel",
    "~/Library/Caches/com.turmeric-lang.TrowelEditor",
    "~/Library/Saved Application State/com.turmeric-lang.TrowelEditor.savedState",
  ]
end
