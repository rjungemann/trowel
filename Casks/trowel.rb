cask "trowel" do
  version "0.0.5"
  sha256 "015757629212e263c7c278cc9201efd6d5a2afb5706141d4bc2cbf0a7768df02"

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
