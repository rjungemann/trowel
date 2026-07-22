cask "trowel" do
  version "0.0.6"
  sha256 "0b9e3b3ea5e1457d5b6d912599d0a69053c9c5a72416c4018be0f04b09b9f5d1"

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
