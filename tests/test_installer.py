"""All installer tests use synthetic files and temporary XDG/HOME roots. Never the live game."""
import argparse,importlib.util,json,os,sys,tempfile,unittest
from pathlib import Path
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT))
import bridge_config as cfg
import install as installer

class InstallerTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.base=Path(self.temp.name)
        self.environment=patch.dict(os.environ,{'HOME':str(self.base/'home'),'XDG_CONFIG_HOME':str(self.base/'config'),'XDG_DATA_HOME':str(self.base/'data'),'XDG_STATE_HOME':str(self.base/'state'),'XDG_RUNTIME_DIR':str(self.base/'runtime'),'LILITH_CONFIG':str(self.base/'config/lilith-linux/config.json')})
        self.environment.start();self.addCleanup(self.environment.stop);self.addCleanup(self.temp.cleanup)
        self.root=self.base/'bridge с пробелами';(self.root/'build').mkdir(parents=True)
        (self.root/'build/version.dll').write_bytes(b'MZ-own-bridge');(self.root/'build/lilith-host').write_text('#!/bin/sh\nexit 0\n');(self.root/'build/lilith-host').chmod(0o755);(self.root/'build/shm-layout.json').write_text('{}')
        self.steam=self.base/'Steam';self.library=self.base/'Другой диск';self.game=self.library/'steamapps/common/The NOexistenceN of Lilith';self.game.mkdir(parents=True)
        (self.game/'Lilith.exe').write_bytes(b'MZ-fake-game');(self.game/'GameAssembly.dll').write_bytes(b'fixture-only')
        (self.library/'steamapps/compatdata/4643090/pfx').mkdir(parents=True);(self.steam/'steamapps').mkdir(parents=True)
        (self.steam/'steamapps/libraryfolders.vdf').write_text('"libraryfolders" { "0" { "path" "'+str(self.library)+'" } }')
        (self.library/'steamapps/appmanifest_4643090.acf').write_text('"AppState" { "appid" "4643090" "installdir" "The NOexistenceN of Lilith" "buildid" "fixture" }')
        self.proton=self.base/'Proton Test/proton';self.proton.parent.mkdir();self.proton.write_text('#!/bin/sh\nexit 0\n');self.proton.chmod(0o755)
        self.args=argparse.Namespace(game_dir=None,steam_root=str(self.steam),compat_data=None,proton=str(self.proton),accept_untested_game=True,replace_existing=False,dry_run=False,uninstall=False)
        self.root_patch=patch.object(installer,'ROOT',self.root);self.root_patch.start();self.addCleanup(self.root_patch.stop)
    def test_discovery_unicode_library(self):
        out=cfg.make_config(self.args);self.assertEqual(out['game_dir'],str(self.game));self.assertEqual(out['compat_data'],str(self.library/'steamapps/compatdata/4643090'));self.assertFalse(out['verified_game_build'])
    def test_unknown_game_rejected_without_opt_in(self):
        self.args.accept_untested_game=False
        with self.assertRaisesRegex(ValueError,'Unverified'):installer.install(self.args)
        self.assertFalse((self.game/'version.dll').exists());self.assertFalse(cfg.config_path().exists())
    def test_dry_run_has_no_writes(self):
        self.args.dry_run=True;installer.install(self.args)
        self.assertFalse((self.game/'version.dll').exists());self.assertFalse(cfg.config_path().exists());self.assertFalse(installer.record_path().exists())
    def test_install_and_uninstall_no_original(self):
        installer.install(self.args);self.assertEqual((self.game/'version.dll').read_bytes(),b'MZ-own-bridge')
        self.assertFalse((self.base/'config/autostart/org.lilith.LinuxBridge.desktop').exists());self.assertIn('X-Lilith-Managed=true',installer.desktop_path().read_text())
        installer.uninstall();self.assertFalse((self.game/'version.dll').exists());self.assertFalse(installer.desktop_path().exists());self.assertTrue((self.game/'GameAssembly.dll').exists())
    def test_unmanaged_files_require_explicit_replacement_and_restore(self):
        target=self.game/'version.dll';target.write_bytes(b'original proxy');desk=installer.desktop_path();desk.parent.mkdir(parents=True);desk.write_text('original desktop')
        with self.assertRaisesRegex(ValueError,'unmanaged'):installer.install(self.args)
        self.args.replace_existing=True;installer.install(self.args);installer.uninstall()
        self.assertEqual(target.read_bytes(),b'original proxy');self.assertEqual(desk.read_text(),'original desktop')
    def test_updates_keep_first_backup(self):
        target=self.game/'version.dll';target.write_bytes(b'original');self.args.replace_existing=True;installer.install(self.args)
        (self.root/'build/version.dll').write_bytes(b'MZ-version-two');installer.install(self.args);installer.uninstall();self.assertEqual(target.read_bytes(),b'original')
    def test_uninstall_preserves_changed_files(self):
        installer.install(self.args);target=self.game/'version.dll';target.write_bytes(b'newer unrelated file')
        with self.assertRaisesRegex(ValueError,'changed'):installer.uninstall()
        self.assertEqual(target.read_bytes(),b'newer unrelated file');self.assertTrue(installer.desktop_path().exists())
    def test_symlink_rejected(self):
        other=self.base/'unrelated';other.write_bytes(b'do not modify');(self.game/'version.dll').symlink_to(other);self.args.replace_existing=True
        with self.assertRaisesRegex(ValueError,'symbolic'):installer.install(self.args)
        self.assertEqual(other.read_bytes(),b'do not modify')
    def test_environment_keeps_other_overrides(self):
        with patch.dict(os.environ,{'WINEDLLOVERRIDES':'foo=b;version=b;bar=n'}):
            env=cfg.game_environment(cfg.make_config(self.args));self.assertEqual(env['WINEDLLOVERRIDES'],'foo=b;bar=n;version=n,b');self.assertEqual(env['LC_ALL'],'C.UTF-8')
    def test_vdf_errors_and_preserved_unicode(self):
        self.assertEqual(cfg.parse_vdf('"a" { "path" "Музыка" }')['a']['path'],'Музыка')
        with self.assertRaises(ValueError):cfg.parse_vdf('"a" { "broken"')
    def test_desktop_escaping(self):
        self.assertIn('%%',cfg.desktop_entry(self.base/'a%path'))
        self.assertIn('Exec="',cfg.desktop_entry(self.root));self.assertIn('" start',cfg.desktop_entry(self.root))
if __name__=='__main__':unittest.main()
