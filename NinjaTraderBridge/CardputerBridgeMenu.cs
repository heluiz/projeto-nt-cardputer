// =====================================================================
//  CardputerBridgeMenu  -  arquivo OPCIONAL
//
//  Coloca "Painel Cardputer" no menu Tools do Control Center, abrindo
//  a pagina de configuracao no navegador.
//
//  Se o F5 acusar erro NESTE arquivo, apague-o. O CardputerBridge.cs
//  funciona sozinho; voce so perde o atalho de menu e acessa direto
//  por http://localhost:8088/
//
//  Instalacao: mesma pasta do outro arquivo
//    Documents\NinjaTrader 8\bin\Custom\AddOns\
// =====================================================================

#region Using declarations
using System;
using System.Diagnostics;
using System.IO;
using System.Windows;
using NinjaTrader.Gui;
using NinjaTrader.Gui.Tools;
#endregion

namespace NinjaTrader.NinjaScript.AddOns
{
    public class CardputerBridgeMenu : NinjaTrader.NinjaScript.AddOnBase
    {
        private NTMenuItem myItem;
        private NTMenuItem parentItem;

        protected override void OnStateChange()
        {
            if (State == State.SetDefaults)
            {
                Name = "CardputerBridgeMenu";
                Description = "Atalho para o painel de configuracao do CardputerBridge.";
            }
        }

        protected override void OnWindowCreated(Window window)
        {
            ControlCenter cc = window as ControlCenter;
            if (cc == null) return;

            try
            {
                parentItem = cc.FindFirst("ControlCenterMenuItemTools") as NTMenuItem;
                if (parentItem == null)
                    parentItem = cc.FindFirst("ControlCenterMenuItemNew") as NTMenuItem;
                if (parentItem == null) return;

                myItem = new NTMenuItem
                {
                    Header = "Painel Cardputer",
                    Style  = Application.Current.TryFindResource("MainMenuItem") as Style
                };

                parentItem.Items.Add(myItem);
                myItem.Click += OnClick;
            }
            catch { }
        }

        protected override void OnWindowDestroyed(Window window)
        {
            try
            {
                if (myItem == null) return;
                if (!(window is ControlCenter)) return;

                if (parentItem != null && parentItem.Items.Contains(myItem))
                    parentItem.Items.Remove(myItem);

                myItem.Click -= OnClick;
                myItem = null;
                parentItem = null;
            }
            catch { }
        }

        private void OnClick(object sender, RoutedEventArgs e)
        {
            try { Process.Start("http://localhost:" + ReadPort() + "/"); }
            catch { }
        }

        // Le a porta do mesmo arquivo de configuracao usado pelo bridge.
        private static int ReadPort()
        {
            int port = 8088;
            try
            {
                string dir = null;
                try { dir = NinjaTrader.Core.Globals.UserDataDir; } catch { }
                if (string.IsNullOrEmpty(dir))
                    dir = Path.Combine(
                        Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments),
                        "NinjaTrader 8");

                string path = Path.Combine(dir, "CardputerBridge.cfg");
                if (!File.Exists(path)) return port;

                foreach (string raw in File.ReadAllLines(path))
                {
                    string line = raw.Trim();
                    if (!line.StartsWith("port=")) continue;
                    int p;
                    if (int.TryParse(line.Substring(5).Trim(), out p) && p > 0 && p < 65536)
                        port = p;
                }
            }
            catch { }
            return port;
        }
    }
}
