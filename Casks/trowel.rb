cask "trowel" do
  version "0.0.10"
  sha256 "0a721036a0941f9c2568480246587b82b4358207f66b8b5a8d6dabe871152ecc"

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
