cask "trowel" do
  version "0.1.3"
  sha256 "94d4af64ea56d8353f6ddffa811857bf8113c1bb217a323bf3bfd8e7f16e4c6e"

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
