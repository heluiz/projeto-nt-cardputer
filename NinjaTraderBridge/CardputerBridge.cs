// =====================================================================
//  CardputerBridge v2  -  AddOn para NinjaTrader 8
//
//  Expoe cotacoes, posicoes e PnL via HTTP na rede local, para o
//  painel CardputerTrader rodando no M5Cardputer ADV.
//
//  Novidades da v2:
//    - Contratos resolvidos automaticamente (rolagem trimestral sozinha)
//    - Le TODAS as contas abertas, com agregacao ou filtro por conta
//    - Painel de configuracao no navegador, com dropdowns
//    - Configuracao persiste em arquivo, sem precisar recompilar
//
//  Instalacao:
//    Documents\NinjaTrader 8\bin\Custom\AddOns\CardputerBridge.cs
//    NinjaScript Editor -> F5
//
//  Configuracao:  http://localhost:8088/
//  Dados:         http://localhost:8088/quote
//
//  por Heluiz
// =====================================================================

#region Using declarations
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using NinjaTrader.Cbi;
using NinjaTrader.Data;
using NinjaTrader.NinjaScript;
#endregion

namespace NinjaTrader.NinjaScript.AddOns
{
    public class CardputerBridge : NinjaTrader.NinjaScript.AddOnBase
    {
        // =============================================================
        //  Padroes (so valem na primeira execucao; depois manda o arquivo
        //  de configuracao, editavel pelo painel web)
        // =============================================================
        private const string DEFAULT_ROOTS = "NQ,ES";
        private const string ACCOUNT_ALL   = "TODAS";
        private const int    DEFAULT_PORT  = 8088;

        private const int SPARK_MAX          = 120;
        private const int SPARK_INTERVAL_MS  = 5000;
        private const int SNAPSHOT_MS        = 500;
        private const int DAILY_REFRESH_MS   = 3600000;  // 1 h (era 5 min: pesava no feed)
        private const int DAY_INTERVAL_MS    = 30000;    // 1 ponto a cada 30s no historico do dia
        private const int DAY_MAX            = 2880;     // 24 h de pontos
        private const int DAY_SEND           = 160;      // pontos enviados ao Cardputer
        private const int EXEC_MAX           = 10;       // ultimas execucoes
        private const int ROLL_CHECK_MS      = 3600000;  // 1 h
        private const int DISCOVERY_PORT     = 8089;     // broadcast: o Cardputer acha o PC sozinho

        // Raizes oferecidas no dropdown do painel web
        private static readonly string[] SUGGESTED_ROOTS =
        {
            "NQ", "MNQ", "ES", "MES", "YM", "MYM", "RTY", "M2K",
            "CL", "MCL", "GC", "MGC", "SI", "NG", "ZB", "ZN", "6E", "6J"
        };

        // =============================================================
        //  Estado
        // =============================================================
        private class Feed
        {
            public string Root;              // "NQ"
            public string Resolved;          // "NQ 09-26"
            public Instrument Instrument;
            public double Last, Bid, Ask;
            public double Open, High, Low, PrevClose;
            public int Decimals = 2;
            public readonly List<double> Spark = new List<double>();
            public DateTime LastSparkAt = DateTime.MinValue;

            public readonly List<double> Day = new List<double>();
            public DateTime LastDayAt = DateTime.MinValue;
            public DateTime DayStamp = DateTime.MinValue;

            public int    Tick = 0;        // +1 subiu, -1 caiu, 0 igual
            public double AlertLevel = 0;
            public int    AlertDir = 0;    // 1 = acima, -1 = abaixo
            public bool   AlertHit = false;
        }

        private readonly List<Feed> feeds = new List<Feed>();
        private readonly object gate = new object();

        private string cfgRoots   = DEFAULT_ROOTS;
        private string cfgAccount = ACCOUNT_ALL;
        private string cfgToken   = "";
        private int    cfgPort    = DEFAULT_PORT;
        private bool   cfgSkipSim = true;    // ignora contas de simulacao
        private bool   cfgSkipOff = true;    // ignora contas desconectadas
        private string cfgIgnore  = "";      // nomes extras a ignorar, separados por virgula
        private string cfgAlerts  = "";      // ex: NQ>23200,ES<5800

        private TcpListener listener;
        private Thread acceptThread;
        private UdpClient discoveryUdp;
        private Thread discoveryThread;
        private Timer snapshotTimer, dailyTimer, rollTimer, histTimer;
        private volatile bool running;
        private volatile string cachedJson = "{\"ok\":false,\"msg\":\"iniciando\"}";
        private string cfgPath;

        // =============================================================
        //  Ciclo de vida
        // =============================================================
        protected override void OnStateChange()
        {
            if (State == State.SetDefaults)
            {
                Name = "CardputerBridge";
                Description = "Expoe cotacoes, posicoes e PnL via HTTP para o painel no M5Cardputer.";
            }
            else if (State == State.Configure) StartBridge();
            else if (State == State.Terminated) StopBridge();
        }

        private void Log(string msg)
        {
            try { NinjaTrader.Code.Output.Process("[CardputerBridge] " + msg, PrintTo.OutputTab1); }
            catch { }
        }

        private void StartBridge()
        {
            if (running) return;
            running = true;

            ResolveConfigPath();
            LoadConfig();
            RebuildFeeds();

            try
            {
                listener = new TcpListener(IPAddress.Any, cfgPort);
                listener.Start();
                acceptThread = new Thread(AcceptLoop) { IsBackground = true, Name = "CardputerBridgeAccept" };
                acceptThread.Start();
                Log("No ar. Configuracao: http://localhost:" + cfgPort + "/");
            }
            catch (Exception ex)
            {
                Log("Nao consegui abrir a porta " + cfgPort + ": " + ex.Message);
            }

            StartDiscovery();

            snapshotTimer = new Timer(delegate { BuildSnapshot(); }, null, 1000, SNAPSHOT_MS);
            dailyTimer    = new Timer(delegate { RefreshDaily(); }, null, 2000, DAILY_REFRESH_MS);
            rollTimer     = new Timer(delegate { CheckRollover(); }, null, ROLL_CHECK_MS, ROLL_CHECK_MS);
            histTimer     = new Timer(delegate { SaveDayHistory(); }, null, 60000, 60000);
        }

        private void StopBridge()
        {
            running = false;
            try { if (snapshotTimer != null) snapshotTimer.Dispose(); } catch { }
            try { if (dailyTimer != null) dailyTimer.Dispose(); } catch { }
            try { if (rollTimer != null) rollTimer.Dispose(); } catch { }
            try { if (histTimer != null) histTimer.Dispose(); } catch { }
            try { SaveDayHistory(); } catch { }
            snapshotTimer = dailyTimer = rollTimer = histTimer = null;

            UnsubscribeAll();

            try { if (listener != null) listener.Stop(); } catch { }
            listener = null;

            try { if (discoveryUdp != null) discoveryUdp.Close(); } catch { }
            discoveryUdp = null;

            Log("Encerrado.");
        }

        // =============================================================
        //  Descoberta automatica (broadcast UDP)
        //  O Cardputer manda "CARDPUTER?" em broadcast; respondemos com
        //  a porta. Ele descobre o IP pelo remetente da resposta.
        // =============================================================
        private void StartDiscovery()
        {
            try
            {
                discoveryUdp = new UdpClient();
                discoveryUdp.ExclusiveAddressUse = false;
                discoveryUdp.Client.SetSocketOption(SocketOptionLevel.Socket,
                                                    SocketOptionName.ReuseAddress, true);
                discoveryUdp.Client.Bind(new IPEndPoint(IPAddress.Any, DISCOVERY_PORT));

                discoveryThread = new Thread(DiscoveryLoop)
                {
                    IsBackground = true,
                    Name = "CardputerBridgeDiscovery"
                };
                discoveryThread.Start();
                Log("Descoberta automatica ativa na porta UDP " + DISCOVERY_PORT);
            }
            catch (Exception ex)
            {
                Log("Descoberta automatica indisponivel: " + ex.Message +
                    " (o Cardputer ainda funciona informando o IP na mao)");
            }
        }

        private void DiscoveryLoop()
        {
            IPEndPoint remote = new IPEndPoint(IPAddress.Any, 0);
            while (running)
            {
                try
                {
                    byte[] data = discoveryUdp.Receive(ref remote);
                    string msg = Encoding.ASCII.GetString(data);
                    if (!msg.StartsWith("CARDPUTER?")) continue;

                    byte[] reply = Encoding.ASCII.GetBytes("CARDPUTER:" + cfgPort);
                    discoveryUdp.Send(reply, reply.Length, remote);
                    Log("Cardputer encontrou o bridge a partir de " + remote.Address);
                }
                catch
                {
                    if (!running) break;
                    Thread.Sleep(300);
                }
            }
        }

        // =============================================================
        //  Configuracao em arquivo
        // =============================================================
        private void ResolveConfigPath()
        {
            string dir = null;
            try { dir = NinjaTrader.Core.Globals.UserDataDir; } catch { }
            if (string.IsNullOrEmpty(dir))
            {
                dir = Path.Combine(
                    Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments),
                    "NinjaTrader 8");
            }
            cfgPath = Path.Combine(dir, "CardputerBridge.cfg");
        }

        private void LoadConfig()
        {
            try
            {
                if (!File.Exists(cfgPath)) { SaveConfig(); return; }

                foreach (string raw in File.ReadAllLines(cfgPath))
                {
                    string line = raw.Trim();
                    if (line.Length == 0 || line.StartsWith("#")) continue;
                    int eq = line.IndexOf('=');
                    if (eq <= 0) continue;

                    string k = line.Substring(0, eq).Trim().ToLowerInvariant();
                    string v = line.Substring(eq + 1).Trim();

                    if      (k == "symbols") cfgRoots = v;
                    else if (k == "account") cfgAccount = v;
                    else if (k == "token")   cfgToken = v;
                    else if (k == "skipsim")  cfgSkipSim = (v.ToLowerInvariant() != "false" && v != "0");
                    else if (k == "skipoff")  cfgSkipOff = (v.ToLowerInvariant() != "false" && v != "0");
                    else if (k == "ignore")   cfgIgnore = v;
                    else if (k == "alerts")   cfgAlerts = v;
                    else if (k == "port")    { int p; if (int.TryParse(v, out p) && p > 0 && p < 65536) cfgPort = p; }
                }
                Log("Config carregada de " + cfgPath);
            }
            catch (Exception ex) { Log("Erro lendo config: " + ex.Message); }
        }

        private void SaveConfig()
        {
            try
            {
                StringBuilder sb = new StringBuilder();
                sb.AppendLine("# CardputerBridge - editavel pelo painel web em http://localhost:" + cfgPort + "/");
                sb.AppendLine("symbols=" + cfgRoots);
                sb.AppendLine("account=" + cfgAccount);
                sb.AppendLine("token=" + cfgToken);
                sb.AppendLine("port=" + cfgPort);
                sb.AppendLine("skipsim=" + (cfgSkipSim ? "true" : "false"));
                sb.AppendLine("skipoff=" + (cfgSkipOff ? "true" : "false"));
                sb.AppendLine("ignore=" + cfgIgnore);
                sb.AppendLine("alerts=" + cfgAlerts);
                File.WriteAllText(cfgPath, sb.ToString());
            }
            catch (Exception ex) { Log("Erro salvando config: " + ex.Message); }
        }

        // =============================================================
        //  Resolucao automatica do contrato vigente
        // =============================================================
        private static readonly int[] QUARTERLY = { 3, 6, 9, 12 };

        private Instrument ResolveFrontMonth(string root, out string resolvedName)
        {
            resolvedName = root;

            // Se o usuario escreveu o vencimento na mao, respeita.
            if (root.Contains(" "))
            {
                try
                {
                    Instrument fixedInst = Instrument.GetInstrument(root);
                    if (fixedInst != null) { resolvedName = root; return fixedInst; }
                }
                catch { }
                return null;
            }

            DateTime now = DateTime.Now;

            // Passo 1: meses trimestrais (indices e a maioria dos futuros financeiros)
            Instrument found = TryMonths(root, now, QUARTERLY, ref resolvedName);
            if (found != null) return found;

            // Passo 2: todos os meses (energia, metais, agricolas)
            int[] allMonths = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 };
            return TryMonths(root, now, allMonths, ref resolvedName);
        }

        private Instrument TryMonths(string root, DateTime now, int[] months, ref string resolvedName)
        {
            for (int i = 0; i < 24; i++)
            {
                DateTime probe = now.AddMonths(i);
                if (Array.IndexOf(months, probe.Month) < 0) continue;

                string name = string.Format(CultureInfo.InvariantCulture, "{0} {1:00}-{2:00}",
                                            root, probe.Month, probe.Year % 100);

                Instrument inst = null;
                try { inst = Instrument.GetInstrument(name); } catch { }
                if (inst == null) continue;

                // Rola 8 dias antes do vencimento, para nao ficar preso num
                // contrato que ja perdeu liquidez.
                DateTime exp = DateTime.MinValue;
                try { exp = inst.Expiry; } catch { }

                if (exp == DateTime.MinValue || exp.AddDays(-8) > now)
                {
                    resolvedName = name;
                    return inst;
                }
            }
            return null;
        }

        // =============================================================
        //  Assinaturas
        // =============================================================
        private void UnsubscribeAll()
        {
            List<Feed> copy;
            lock (gate) { copy = feeds.ToList(); feeds.Clear(); }
            foreach (Feed f in copy)
            {
                try
                {
                    if (f.Instrument != null && f.Instrument.MarketData != null)
                        f.Instrument.MarketData.Update -= OnMarketData;
                }
                catch { }
            }
        }

        private void RebuildFeeds()
        {
            UnsubscribeAll();

            string[] roots = cfgRoots.Split(new char[] { ',', ';' }, StringSplitOptions.RemoveEmptyEntries);

            foreach (string rawRoot in roots)
            {
                string root = rawRoot.Trim();
                if (root.Length == 0) continue;

                try
                {
                    string resolved;
                    Instrument inst = ResolveFrontMonth(root, out resolved);
                    if (inst == null)
                    {
                        Log("Nao encontrei contrato vigente para '" + root + "'");
                        continue;
                    }

                    Feed f = new Feed
                    {
                        Root       = root,
                        Resolved   = resolved,
                        Instrument = inst,
                        Decimals   = DecimalsFromTick(inst.MasterInstrument.TickSize)
                    };

                    inst.MarketData.Update += OnMarketData;
                    lock (gate) { feeds.Add(f); }
                    Log("Assinado: " + root + " -> " + resolved);
                }
                catch (Exception ex) { Log("Erro em " + root + ": " + ex.Message); }
            }

            RefreshDaily();
        }

        // Formato: NQ>23200,ES<5800  (um por contrato)
        private void ApplyAlerts()
        {
            List<Feed> snap;
            lock (gate) { snap = feeds.ToList(); }

            foreach (Feed f in snap)
            {
                lock (gate) { f.AlertLevel = 0; f.AlertDir = 0; f.AlertHit = false; }
            }
            if (string.IsNullOrEmpty(cfgAlerts)) return;

            foreach (string raw in cfgAlerts.Split(new char[] { ',', ';' },
                                                  StringSplitOptions.RemoveEmptyEntries))
            {
                string item = raw.Trim();
                int dir = 0, at = -1;
                at = item.IndexOf('>');
                if (at > 0) dir = 1;
                else { at = item.IndexOf('<'); if (at > 0) dir = -1; }
                if (dir == 0 || at <= 0) continue;

                string root = item.Substring(0, at).Trim();
                double lvl;
                if (!double.TryParse(item.Substring(at + 1).Trim(),
                                     NumberStyles.Any, CultureInfo.InvariantCulture, out lvl)) continue;

                foreach (Feed f in snap)
                {
                    if (!string.Equals(f.Root, root, StringComparison.OrdinalIgnoreCase)) continue;
                    lock (gate) { f.AlertLevel = lvl; f.AlertDir = dir; }
                }
            }
        }

        // Se o contrato vigente mudou (rolagem), reassina sozinho.
        private void CheckRollover()
        {
            if (!running) return;
            try
            {
                List<Feed> snap;
                lock (gate) { snap = feeds.ToList(); }

                bool changed = false;
                foreach (Feed f in snap)
                {
                    if (f.Root.Contains(" ")) continue;   // travado pelo usuario
                    string resolved;
                    Instrument inst = ResolveFrontMonth(f.Root, out resolved);
                    if (inst != null && resolved != f.Resolved) { changed = true; break; }
                }

                if (changed)
                {
                    Log("Rolagem detectada. Reassinando contratos.");
                    RebuildFeeds();
                }
            }
            catch { }
        }

        private static int DecimalsFromTick(double tick)
        {
            if (tick <= 0) return 2;
            for (int d = 0; d <= 8; d++)
            {
                double scaled = tick * Math.Pow(10, d);
                if (Math.Abs(scaled - Math.Round(scaled)) < 1e-9) return d;
            }
            return 2;
        }

        // =============================================================
        //  Market data
        // =============================================================
        private void OnMarketData(object sender, MarketDataEventArgs e)
        {
            if (!running || e == null || e.Instrument == null) return;
            try
            {
                lock (gate)
                {
                    Feed f = null;
                    for (int i = 0; i < feeds.Count; i++)
                    {
                        if (feeds[i].Instrument != null &&
                            feeds[i].Instrument.FullName == e.Instrument.FullName)
                        { f = feeds[i]; break; }
                    }
                    if (f == null) return;

                    double p = e.Price;

                    if (e.MarketDataType == MarketDataType.Last)
                    {
                        if (p <= 0) return;

                        if (f.Last > 0) f.Tick = (p > f.Last) ? 1 : (p < f.Last ? -1 : f.Tick);
                        f.Last = p;

                        if (f.AlertDir != 0 && f.AlertLevel > 0)
                            f.AlertHit = (f.AlertDir > 0) ? (p >= f.AlertLevel) : (p <= f.AlertLevel);
                        if (f.High <= 0 || p > f.High) f.High = p;
                        if (f.Low  <= 0 || p < f.Low)  f.Low  = p;
                        if (f.Open <= 0) f.Open = p;

                        DateTime now = DateTime.Now;
                        if ((now - f.LastSparkAt).TotalMilliseconds >= SPARK_INTERVAL_MS)
                        {
                            f.LastSparkAt = now;
                            f.Spark.Add(p);
                            while (f.Spark.Count > SPARK_MAX) f.Spark.RemoveAt(0);
                        }

                        if ((now - f.LastDayAt).TotalMilliseconds >= DAY_INTERVAL_MS)
                        {
                            f.LastDayAt = now;
                            if (f.DayStamp.Date != now.Date) { f.Day.Clear(); f.DayStamp = now; }
                            f.Day.Add(p);
                            while (f.Day.Count > DAY_MAX) f.Day.RemoveAt(0);
                        }
                    }
                    else if (e.MarketDataType == MarketDataType.Bid) { if (p > 0) f.Bid = p; }
                    else if (e.MarketDataType == MarketDataType.Ask) { if (p > 0) f.Ask = p; }
                }
            }
            catch { }
        }

        private void RefreshDaily()
        {
            if (!running) return;
            List<Feed> snapshot;
            lock (gate) { snapshot = feeds.ToList(); }

            foreach (Feed f in snapshot)
            {
                Feed target = f;
                try
                {
                    BarsRequest br = new BarsRequest(target.Instrument, 5);
                    br.BarsPeriod = new BarsPeriod { BarsPeriodType = BarsPeriodType.Day, Value = 1 };
                    br.TradingHours = target.Instrument.MasterInstrument.TradingHours;

                    br.Request(new Action<BarsRequest, ErrorCode, string>(delegate (BarsRequest req, ErrorCode err, string msg)
                    {
                        try
                        {
                            if (err != ErrorCode.NoError || req == null || req.Bars == null) return;
                            int cnt = req.Bars.Count;
                            if (cnt <= 0) return;

                            double o  = req.Bars.GetOpen(cnt - 1);
                            double h  = req.Bars.GetHigh(cnt - 1);
                            double l  = req.Bars.GetLow(cnt - 1);
                            double pc = (cnt >= 2) ? req.Bars.GetClose(cnt - 2) : 0;

                            lock (gate)
                            {
                                if (o > 0) target.Open = o;
                                if (h > 0) target.High = (target.High > 0) ? Math.Max(target.High, h) : h;
                                if (l > 0) target.Low  = (target.Low  > 0) ? Math.Min(target.Low, l)  : l;
                                if (pc > 0) target.PrevClose = pc;
                            }
                        }
                        catch { }
                        finally { try { if (req != null) req.Dispose(); } catch { } }
                    }));
                }
                catch (Exception ex) { Log("BarsRequest " + target.Resolved + ": " + ex.Message); }
            }
        }

        // =============================================================
        //  Historico da sessao em disco
        //  Sobrevive a reinicio do NinjaTrader.
        // =============================================================
        private string HistDir()
        {
            string dir = Path.Combine(Path.GetDirectoryName(cfgPath), "CardputerHistory");
            try { if (!Directory.Exists(dir)) Directory.CreateDirectory(dir); } catch { }
            return dir;
        }

        private string HistFile(string root)
        {
            string safe = root.Replace(" ", "_").Replace(":", "");
            return Path.Combine(HistDir(), safe + "_" + DateTime.Now.ToString("yyyyMMdd") + ".csv");
        }

        private void LoadDayHistory()
        {
            List<Feed> snap;
            lock (gate) { snap = feeds.ToList(); }

            foreach (Feed f in snap)
            {
                try
                {
                    string path = HistFile(f.Root);
                    if (!File.Exists(path)) continue;

                    List<double> pts = new List<double>();
                    foreach (string line in File.ReadAllLines(path))
                    {
                        string t = line.Trim();
                        if (t.Length == 0) continue;
                        int c = t.IndexOf(',');
                        string val = (c > 0) ? t.Substring(c + 1) : t;
                        double d;
                        if (double.TryParse(val, NumberStyles.Any, CultureInfo.InvariantCulture, out d) && d > 0)
                            pts.Add(d);
                    }
                    while (pts.Count > DAY_MAX) pts.RemoveAt(0);

                    lock (gate)
                    {
                        f.Day.Clear();
                        f.Day.AddRange(pts);
                        f.DayStamp = DateTime.Now;
                    }
                    Log("Historico do dia recuperado: " + f.Root + " (" + pts.Count + " pontos)");
                }
                catch { }
            }
        }

        private void SaveDayHistory()
        {
            List<Feed> snap;
            lock (gate) { snap = feeds.ToList(); }

            foreach (Feed f in snap)
            {
                double[] pts;
                lock (gate) { pts = f.Day.ToArray(); }
                if (pts.Length == 0) continue;

                try
                {
                    StringBuilder sb2 = new StringBuilder(pts.Length * 12);
                    for (int i = 0; i < pts.Length; i++)
                        sb2.Append(i).Append(',').Append(N(pts[i], 6)).Append("\r\n");
                    File.WriteAllText(HistFile(f.Root), sb2.ToString());
                }
                catch { }
            }
        }

        // =============================================================
        //  Contas
        // =============================================================
        private List<Account> AllAccounts()
        {
            List<Account> list = new List<Account>();
            try
            {
                lock (Account.All)
                {
                    foreach (Account a in Account.All) if (a != null) list.Add(a);
                }
            }
            catch { }
            return list;
        }

        // Contas de simulacao do NinjaTrader nascem como Sim### e Playback###.
        // O usuario pode acrescentar outros nomes na lista "ignore".
        private bool IsIgnored(Account a)
        {
            if (a == null || string.IsNullOrEmpty(a.Name)) return true;
            string n = a.Name.Trim();
            string nl = n.ToLowerInvariant();

            if (cfgSkipSim && (nl.StartsWith("sim") || nl.StartsWith("playback") ||
                               nl.StartsWith("backtest") || nl.StartsWith("replay")))
                return true;

            // Conta desconectada mostra saldo congelado. Fora do calculo.
            if (cfgSkipOff)
            {
                bool online = false;
                try { online = (a.Connection != null); } catch { }
                if (!online) return true;
            }

            if (!string.IsNullOrEmpty(cfgIgnore))
            {
                foreach (string raw in cfgIgnore.Split(new char[] { ',', ';' },
                                                      StringSplitOptions.RemoveEmptyEntries))
                {
                    if (string.Equals(raw.Trim(), n, StringComparison.OrdinalIgnoreCase)) return true;
                }
            }
            return false;
        }

        private List<Account> SelectedAccounts()
        {
            List<Account> all = AllAccounts().Where(a => !IsIgnored(a)).ToList();
            if (string.IsNullOrEmpty(cfgAccount) || cfgAccount == ACCOUNT_ALL) return all;
            return all.Where(a => a.Name == cfgAccount).ToList();
        }

        private static string N(double v, int dec)
        {
            if (double.IsNaN(v) || double.IsInfinity(v)) return "0";
            return Math.Round(v, dec).ToString("0.########", CultureInfo.InvariantCulture);
        }

        private static string Esc(string s)
        {
            if (string.IsNullOrEmpty(s)) return "";
            return s.Replace("\\", "\\\\").Replace("\"", "\\\"");
        }

        private static string Html(string s)
        {
            if (string.IsNullOrEmpty(s)) return "";
            return s.Replace("&", "&amp;").Replace("<", "&lt;").Replace(">", "&gt;").Replace("\"", "&quot;");
        }

        // =============================================================
        //  Snapshot JSON
        // =============================================================
        private void BuildSnapshot()
        {
            if (!running) return;
            try
            {
                List<Account> sel = SelectedAccounts();

                double aggReal = 0, aggUnreal = 0;
                StringBuilder accJson = new StringBuilder();
                accJson.Append('[');

                for (int i = 0; i < sel.Count; i++)
                {
                    Account a = sel[i];
                    double r = 0, u = 0;
                    bool live = false;
                    try
                    {
                        r = a.Get(AccountItem.RealizedProfitLoss, Currency.UsDollar);
                        u = a.Get(AccountItem.UnrealizedProfitLoss, Currency.UsDollar);
                        live = (a.Connection != null);
                    }
                    catch { }

                    aggReal += r;
                    aggUnreal += u;

                    if (i > 0) accJson.Append(',');
                    accJson.Append("{\"name\":\"").Append(Esc(a.Name)).Append("\",");
                    accJson.Append("\"conn\":").Append(live ? "true" : "false").Append(',');
                    accJson.Append("\"realized\":").Append(N(r, 2)).Append(',');
                    accJson.Append("\"unrealized\":").Append(N(u, 2)).Append(',');
                    accJson.Append("\"total\":").Append(N(r + u, 2)).Append('}');
                }
                accJson.Append(']');

                StringBuilder sb = new StringBuilder(16384);
                sb.Append("{\"ok\":true,");
                sb.Append("\"mode\":\"").Append(Esc(cfgAccount)).Append("\",");
                sb.Append("\"nAcc\":").Append(sel.Count).Append(',');
                sb.Append("\"accOk\":").Append(sel.Count > 0 ? "true" : "false").Append(',');
                sb.Append("\"realized\":").Append(N(aggReal, 2)).Append(',');
                sb.Append("\"unrealized\":").Append(N(aggUnreal, 2)).Append(',');
                sb.Append("\"total\":").Append(N(aggReal + aggUnreal, 2)).Append(',');
                sb.Append("\"accounts\":").Append(accJson).Append(',');
                sb.Append("\"quotes\":[");

                List<Feed> snapshot;
                lock (gate) { snapshot = feeds.ToList(); }

                for (int i = 0; i < snapshot.Count; i++)
                {
                    Feed f = snapshot[i];
                    if (i > 0) sb.Append(',');

                    double last, open, high, low, prevClose, bid, ask;
                    int dec;
                    double[] spark;
                    string resolved;

                    lock (gate)
                    {
                        last = f.Last; open = f.Open; high = f.High; low = f.Low;
                        prevClose = f.PrevClose; bid = f.Bid; ask = f.Ask; dec = f.Decimals;
                        spark = f.Spark.ToArray(); resolved = f.Resolved;
                    }

                    double baseline = prevClose > 0 ? prevClose : open;
                    double chg    = (baseline > 0 && last > 0) ? (last - baseline) : 0;
                    double chgPct = (baseline > 0 && last > 0) ? (chg / baseline * 100.0) : 0;

                    // Posicao agregada entre as contas selecionadas
                    int   netQty  = 0;
                    double notion = 0;   // soma de qty*preco, para preco medio ponderado
                    double upl    = 0;

                    foreach (Account a in sel)
                    {
                        try
                        {
                            Position p = null;
                            lock (a.Positions)
                            {
                                p = a.Positions.FirstOrDefault(x =>
                                    x.Instrument != null && f.Instrument != null &&
                                    x.Instrument.FullName == f.Instrument.FullName);
                            }
                            if (p == null || p.MarketPosition == MarketPosition.Flat) continue;

                            int q = (p.MarketPosition == MarketPosition.Short) ? -p.Quantity : p.Quantity;
                            netQty += q;
                            notion += Math.Abs(q) * p.AveragePrice;
                            if (last > 0) upl += p.GetUnrealizedProfitLoss(PerformanceUnit.Currency, last);
                        }
                        catch { }
                    }

                    double avgPrice = 0;
                    int absQty = Math.Abs(netQty);
                    if (absQty > 0 && notion > 0) avgPrice = notion / absQty;

                    // quantas ordens de trabalho existem neste contrato
                    int nOrd = 0;
                    foreach (Account a in sel)
                    {
                        try
                        {
                            lock (a.Orders)
                            {
                                foreach (Order od in a.Orders)
                                {
                                    if (od == null || od.Instrument == null || f.Instrument == null) continue;
                                    if (od.OrderState != OrderState.Working &&
                                        od.OrderState != OrderState.Accepted) continue;
                                    if (od.Instrument.FullName == f.Instrument.FullName) nOrd++;
                                }
                            }
                        }
                        catch { }
                    }

                    sb.Append("{\"sym\":\"").Append(Esc(resolved)).Append("\",");
                    sb.Append("\"root\":\"").Append(Esc(f.Root)).Append("\",");
                    sb.Append("\"dec\":").Append(dec).Append(',');
                    sb.Append("\"last\":").Append(N(last, dec)).Append(',');
                    sb.Append("\"bid\":").Append(N(bid, dec)).Append(',');
                    sb.Append("\"ask\":").Append(N(ask, dec)).Append(',');
                    sb.Append("\"open\":").Append(N(open, dec)).Append(',');
                    sb.Append("\"high\":").Append(N(high, dec)).Append(',');
                    sb.Append("\"low\":").Append(N(low, dec)).Append(',');
                    sb.Append("\"prev\":").Append(N(prevClose, dec)).Append(',');
                    sb.Append("\"chg\":").Append(N(chg, dec)).Append(',');
                    sb.Append("\"pct\":").Append(N(chgPct, 2)).Append(',');
                    sb.Append("\"pos\":").Append(netQty).Append(',');
                    sb.Append("\"avg\":").Append(N(avgPrice, dec)).Append(',');
                    sb.Append("\"upl\":").Append(N(upl, 2)).Append(',');
                    sb.Append("\"nord\":").Append(nOrd).Append(',');
                    sb.Append("\"tick\":").Append(f.Tick).Append(',');
                    sb.Append("\"alert\":").Append(f.AlertHit ? 1 : 0).Append(',');
                    sb.Append("\"alvl\":").Append(N(f.AlertLevel, dec)).Append(',');
                    // Com muitos contratos o JSON fica pesado para o ESP32.
                    // A partir de 5 feeds, mandamos 1 ponto a cada 2.
                    int stepSpark = (snapshot.Count > 4) ? 2 : 1;
                    sb.Append("\"sint\":").Append(stepSpark * (SPARK_INTERVAL_MS / 1000)).Append(',');

                    sb.Append("\"spark\":[");
                    bool firstSpark = true;
                    for (int k = 0; k < spark.Length; k += stepSpark)
                    {
                        if (!firstSpark) sb.Append(',');
                        sb.Append(N(spark[k], dec));
                        firstSpark = false;
                    }
                    sb.Append("]}");
                }

                sb.Append("],");

                // ---- estrategias rodando ----
                sb.Append("\"strats\":[");
                int ns = 0;
                foreach (Account a in sel)
                {
                    List<object> list = new List<object>();
                    try { lock (a.Strategies) { foreach (object o in a.Strategies) list.Add(o); } }
                    catch { }

                    foreach (object o in list)
                    {
                        StrategyBase sbx = o as StrategyBase;
                        if (sbx == null) continue;
                        if (ns > 0) sb.Append(',');

                        string stName = "?", stState = "?";
                        try { stName = sbx.Name; } catch { }
                        try { stState = sbx.State.ToString(); } catch { }

                        sb.Append("{\"name\":\"").Append(Esc(stName)).Append("\",");
                        sb.Append("\"acct\":\"").Append(Esc(a.Name)).Append("\",");
                        sb.Append("\"state\":\"").Append(Esc(stState)).Append("\"}");
                        ns++;
                        if (ns >= 16) break;
                    }
                    if (ns >= 16) break;
                }
                sb.Append("],");

                // ---- TODAS as posicoes abertas, mesmo de contratos fora da lista ----
                sb.Append("\"positions\":[");
                int np = 0;
                foreach (Account a in sel)
                {
                    List<Position> plist = new List<Position>();
                    try
                    {
                        lock (a.Positions)
                        {
                            foreach (Position p in a.Positions)
                                if (p != null && p.MarketPosition != MarketPosition.Flat) plist.Add(p);
                        }
                    }
                    catch { }

                    foreach (Position p in plist)
                    {
                        if (np >= 20) break;
                        if (np > 0) sb.Append(',');

                        string psym = "?";
                        int pqty = 0;
                        double pavg = 0, pupl = 0;

                        try { psym = (p.Instrument != null) ? p.Instrument.MasterInstrument.Name : "?"; } catch { }
                        try { pqty = (p.MarketPosition == MarketPosition.Short) ? -p.Quantity : p.Quantity; } catch { }
                        try { pavg = p.AveragePrice; } catch { }
                        try
                        {
                            double px = 0;
                            lock (gate)
                            {
                                foreach (Feed ff in feeds)
                                    if (ff.Instrument != null && p.Instrument != null &&
                                        ff.Instrument.FullName == p.Instrument.FullName) { px = ff.Last; break; }
                            }
                            if (px <= 0 && p.Instrument != null) px = p.Instrument.MarketData != null &&
                                                                     p.Instrument.MarketData.Last != null
                                                                   ? p.Instrument.MarketData.Last.Price : 0;
                            if (px > 0) pupl = p.GetUnrealizedProfitLoss(PerformanceUnit.Currency, px);
                        }
                        catch { }

                        sb.Append("{\"sym\":\"").Append(Esc(psym)).Append("\",");
                        sb.Append("\"acct\":\"").Append(Esc(a.Name)).Append("\",");
                        sb.Append("\"qty\":").Append(pqty).Append(',');
                        sb.Append("\"avg\":").Append(N(pavg, 4)).Append(',');
                        sb.Append("\"upl\":").Append(N(pupl, 2)).Append('}');
                        np++;
                    }
                    if (np >= 20) break;
                }
                sb.Append("],");

                // ---- ordens de trabalho ----
                sb.Append("\"orders\":[");
                int no = 0;
                foreach (Account a in sel)
                {
                    List<Order> ords = new List<Order>();
                    try
                    {
                        lock (a.Orders)
                        {
                            foreach (Order od in a.Orders)
                            {
                                if (od == null) continue;
                                if (od.OrderState == OrderState.Working ||
                                    od.OrderState == OrderState.Accepted)
                                    ords.Add(od);
                            }
                        }
                    }
                    catch { }

                    foreach (Order od in ords)
                    {
                        if (no >= 20) break;
                        if (no > 0) sb.Append(',');

                        string osym = "?", oact = "?", otyp = "?", ostate = "?";
                        double opx = 0;
                        int oqty = 0;

                        try { osym = (od.Instrument != null) ? od.Instrument.MasterInstrument.Name : "?"; } catch { }
                        try { oact = od.OrderAction.ToString(); } catch { }
                        try { otyp = od.OrderType.ToString(); } catch { }
                        try { ostate = od.OrderState.ToString(); } catch { }
                        try { oqty = od.Quantity; } catch { }
                        try { opx = (od.LimitPrice > 0) ? od.LimitPrice : od.StopPrice; } catch { }

                        sb.Append("{\"sym\":\"").Append(Esc(osym)).Append("\",");
                        sb.Append("\"acct\":\"").Append(Esc(a.Name)).Append("\",");
                        sb.Append("\"act\":\"").Append(Esc(oact)).Append("\",");
                        sb.Append("\"typ\":\"").Append(Esc(otyp)).Append("\",");
                        sb.Append("\"st\":\"").Append(Esc(ostate)).Append("\",");
                        sb.Append("\"qty\":").Append(oqty).Append(',');
                        sb.Append("\"px\":").Append(N(opx, 4)).Append('}');
                        no++;
                    }
                    if (no >= 20) break;
                }
                sb.Append("],");

                // ---- ultimas execucoes ----
                List<Execution> execs = new List<Execution>();
                foreach (Account a in sel)
                {
                    try
                    {
                        lock (a.Executions)
                        {
                            foreach (Execution ex in a.Executions) if (ex != null) execs.Add(ex);
                        }
                    }
                    catch { }
                }

                try { execs = execs.OrderByDescending(x => x.Time).Take(EXEC_MAX).ToList(); }
                catch { if (execs.Count > EXEC_MAX) execs = execs.GetRange(0, EXEC_MAX); }

                sb.Append("\"execs\":[");
                for (int i = 0; i < execs.Count; i++)
                {
                    Execution ex = execs[i];
                    if (i > 0) sb.Append(',');

                    string esym = "?", eside = "?", etime = "";
                    double eprice = 0;
                    int eqty = 0;
                    try { esym = (ex.Instrument != null) ? ex.Instrument.MasterInstrument.Name : "?"; } catch { }
                    try { eside = (ex.MarketPosition == MarketPosition.Long) ? "C" : "V"; } catch { }
                    try { etime = ex.Time.ToString("HH:mm:ss"); } catch { }
                    try { eprice = ex.Price; } catch { }
                    try { eqty = ex.Quantity; } catch { }

                    sb.Append("{\"t\":\"").Append(Esc(etime)).Append("\",");
                    sb.Append("\"sym\":\"").Append(Esc(esym)).Append("\",");
                    sb.Append("\"side\":\"").Append(eside).Append("\",");
                    sb.Append("\"qty\":").Append(eqty).Append(',');
                    sb.Append("\"px\":").Append(N(eprice, 4)).Append('}');
                }
                sb.Append("]}");
                cachedJson = sb.ToString();
            }
            catch (Exception ex)
            {
                cachedJson = "{\"ok\":false,\"msg\":\"" + Esc(ex.Message) + "\"}";
            }
        }

        // =============================================================
        //  Emergencia: zerar posicoes, cancelar ordens, parar estrategias
        //
        //  So roda com token configurado. Sem token, o endpoint recusa.
        // =============================================================
        private string DoPanic(bool flatten, bool killStrategies)
        {
            int cancelled = 0, flattened = 0, strats = 0, errors = 0;
            List<Account> sel = SelectedAccounts();

            foreach (Account a in sel)
            {
                try
                {
                    if (flatten)
                    {
                        // ordens de trabalho
                        List<Order> orders = new List<Order>();
                        try
                        {
                            lock (a.Orders)
                            {
                                foreach (Order o in a.Orders)
                                {
                                    if (o == null) continue;
                                    if (o.OrderState == OrderState.Working ||
                                        o.OrderState == OrderState.Accepted)
                                        orders.Add(o);
                                }
                            }
                            if (orders.Count > 0) { a.Cancel(orders); cancelled += orders.Count; }
                        }
                        catch { errors++; }

                        // posicoes abertas
                        List<Instrument> instrs = new List<Instrument>();
                        try
                        {
                            lock (a.Positions)
                            {
                                foreach (Position p in a.Positions)
                                {
                                    if (p == null || p.Instrument == null) continue;
                                    if (p.MarketPosition == MarketPosition.Flat) continue;
                                    instrs.Add(p.Instrument);
                                }
                            }
                            if (instrs.Count > 0) { a.Flatten(instrs); flattened += instrs.Count; }
                        }
                        catch { errors++; }
                    }

                    if (killStrategies)
                    {
                        List<object> list = new List<object>();
                        try
                        {
                            lock (a.Strategies)
                            {
                                foreach (object o in a.Strategies) list.Add(o);
                            }
                        }
                        catch { errors++; }

                        foreach (object o in list)
                        {
                            StrategyBase sb = o as StrategyBase;
                            if (sb == null) continue;
                            try { sb.SetState(State.Terminated); strats++; }
                            catch { errors++; }
                        }
                    }
                }
                catch (Exception ex)
                {
                    errors++;
                    Log("Panico em " + a.Name + ": " + ex.Message);
                }
            }

            string res = string.Format(CultureInfo.InvariantCulture,
                "ordens {0} / posicoes {1} / estrategias {2}{3}",
                cancelled, flattened, strats, errors > 0 ? " / " + errors + " erros" : "");

            Log("PANICO em " + sel.Count + " conta(s): " + res);
            return res;
        }

        // Historico da sessao, reamostrado para caber no Cardputer.
        private string BuildHistory(string root)
        {
            Feed f = null;
            lock (gate)
            {
                foreach (Feed x in feeds)
                {
                    if (string.Equals(x.Root, root, StringComparison.OrdinalIgnoreCase) ||
                        string.Equals(x.Resolved, root, StringComparison.OrdinalIgnoreCase))
                    { f = x; break; }
                }
                if (f == null && feeds.Count > 0) f = feeds[0];
            }
            if (f == null) return "{\"ok\":false}";

            double[] pts;
            int dec;
            string name;
            lock (gate) { pts = f.Day.ToArray(); dec = f.Decimals; name = f.Root; }

            StringBuilder sb = new StringBuilder(4096);
            sb.Append("{\"ok\":true,\"sym\":\"").Append(Esc(name)).Append("\",");
            sb.Append("\"dec\":").Append(dec).Append(",\"pts\":[");

            if (pts.Length <= DAY_SEND)
            {
                for (int i = 0; i < pts.Length; i++)
                {
                    if (i > 0) sb.Append(',');
                    sb.Append(N(pts[i], dec));
                }
            }
            else
            {
                for (int i = 0; i < DAY_SEND; i++)
                {
                    int idx = (int)((long)i * (pts.Length - 1) / (DAY_SEND - 1));
                    if (i > 0) sb.Append(',');
                    sb.Append(N(pts[idx], dec));
                }
            }
            sb.Append("]}");
            return sb.ToString();
        }

        // =============================================================
        //  Painel de configuracao (HTML)
        // =============================================================
        private string BuildConfigPage()
        {
            StringBuilder sb = new StringBuilder(8192);

            sb.Append("<!doctype html><html lang=\"pt-br\"><head><meta charset=\"utf-8\">");
            sb.Append("<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">");
            sb.Append("<title>CardputerBridge</title><style>");
            sb.Append("body{background:#131316;color:#e8e8ea;font:15px/1.6 system-ui,sans-serif;margin:0;padding:24px}");
            sb.Append(".w{max-width:640px;margin:0 auto}");
            sb.Append("h1{font-size:20px;font-weight:500;margin:0 0 4px}");
            sb.Append(".sub{color:#8a8a92;font-size:13px;margin:0 0 24px}");
            sb.Append(".card{background:#1c1c21;border:1px solid #2a2a31;border-radius:10px;padding:18px;margin-bottom:16px}");
            sb.Append("label{display:block;font-size:13px;color:#8a8a92;margin:14px 0 6px}");
            sb.Append("select,input{width:100%;box-sizing:border-box;background:#131316;color:#e8e8ea;");
            sb.Append("border:1px solid #33333c;border-radius:6px;padding:9px 10px;font-size:14px}");
            sb.Append("button{margin-top:18px;background:#00E676;color:#062;border:0;border-radius:6px;");
            sb.Append("padding:10px 18px;font-size:14px;font-weight:500;cursor:pointer}");
            sb.Append("table{width:100%;border-collapse:collapse;font-size:13px}");
            sb.Append("th{text-align:left;color:#8a8a92;font-weight:400;padding:6px 0;border-bottom:1px solid #2a2a31}");
            sb.Append("td{padding:7px 0;border-bottom:1px solid #232329}");
            sb.Append(".up{color:#00E676}.dn{color:#FF5252}.dim{color:#8a8a92}");
            sb.Append(".hint{font-size:12px;color:#6a6a72;margin-top:6px}");
            sb.Append("</style></head><body><div class=\"w\">");

            sb.Append("<h1>CardputerBridge</h1>");
            sb.Append("<p class=\"sub\">Ajuste conta e contratos. As mudancas valem na hora, sem recompilar.</p>");

            // ---- formulario ----
            sb.Append("<form class=\"card\" method=\"get\" action=\"/set\">");

            sb.Append("<label for=\"account\">Conta</label>");
            sb.Append("<select name=\"account\" id=\"account\">");
            sb.Append("<option value=\"").Append(ACCOUNT_ALL).Append("\"");
            if (cfgAccount == ACCOUNT_ALL) sb.Append(" selected");
            sb.Append(">Todas as contas (agregado)</option>");

            foreach (Account a in AllAccounts())
            {
                string conn = (a.Connection != null) ? "" : "  (desconectada)";
                sb.Append("<option value=\"").Append(Html(a.Name)).Append("\"");
                if (cfgAccount == a.Name) sb.Append(" selected");
                sb.Append('>').Append(Html(a.Name)).Append(Html(conn)).Append("</option>");
            }
            sb.Append("</select>");
            sb.Append("<p class=\"hint\">Com \"todas\", posicoes e PnL sao somados entre as contas.</p>");

            sb.Append("<label for=\"symbols\">Contratos</label>");
            sb.Append("<input name=\"symbols\" id=\"symbols\" list=\"roots\" value=\"")
              .Append(Html(cfgRoots)).Append("\">");
            sb.Append("<datalist id=\"roots\">");
            foreach (string r in SUGGESTED_ROOTS) sb.Append("<option value=\"").Append(r).Append("\">");
            sb.Append("</datalist>");
            sb.Append("<p class=\"hint\">Separe por virgula. Use so a raiz (NQ, ES) que o vencimento vigente e escolhido sozinho, ");
            sb.Append("com rolagem 8 dias antes. Para travar um contrato especifico, escreva completo: <code>NQ 12-26</code>.</p>");

            sb.Append("<label for=\"alerts\">Alertas de preco</label>");
            sb.Append("<input name=\"alerts\" id=\"alerts\" value=\"").Append(Html(cfgAlerts)).Append("\">");
            sb.Append("<p class=\"hint\">Formato <code>NQ&gt;23200,ES&lt;5800</code>. ");
            sb.Append("Quando cruzar, a borda da tela do Cardputer pisca.</p>");

            sb.Append("<label>Contas de simulacao</label>");
            sb.Append("<select name=\"skipsim\">");
            sb.Append("<option value=\"true\"").Append(cfgSkipSim ? " selected" : "").Append(">Ignorar (Sim, Playback, Replay)</option>");
            sb.Append("<option value=\"false\"").Append(cfgSkipSim ? "" : " selected").Append(">Incluir no calculo</option>");
            sb.Append("</select>");

            sb.Append("<label>Contas desconectadas</label>");
            sb.Append("<select name=\"skipoff\">");
            sb.Append("<option value=\"true\"").Append(cfgSkipOff ? " selected" : "").Append(">Ignorar</option>");
            sb.Append("<option value=\"false\"").Append(cfgSkipOff ? "" : " selected").Append(">Incluir no calculo</option>");
            sb.Append("</select>");
            sb.Append("<p class=\"hint\">Desconectada mostra saldo congelado. Ignorar evita somar numero velho.</p>");

            sb.Append("<label for=\"ignore\">Outras contas a ignorar</label>");
            sb.Append("<input name=\"ignore\" id=\"ignore\" value=\"").Append(Html(cfgIgnore)).Append("\">");
            sb.Append("<p class=\"hint\">Nomes exatos, separados por virgula. Deixe vazio para nao ignorar mais nenhuma.</p>");

            sb.Append("<label for=\"token\">Token dos comandos de emergencia</label>");
            sb.Append("<input name=\"token\" id=\"token\" value=\"").Append(Html(cfgToken)).Append("\">");
            sb.Append("<p class=\"hint\">Em branco desativa os comandos de zerar posicao. ");
            sb.Append("O mesmo valor precisa estar no Cardputer (assistente, passo 4).</p>");

            sb.Append("<button type=\"submit\">Salvar e reassinar</button>");
            sb.Append("</form>");

            // ---- comandos de emergencia ----
            sb.Append("<div class=\"card\">");
            sb.Append("<div style=\"font-size:13px;color:#8a8a92;margin-bottom:10px\">Emergencia</div>");

            if (string.IsNullOrEmpty(cfgToken))
            {
                sb.Append("<p class=\"dim\" style=\"margin:0\">Defina um token acima para liberar estes comandos.</p>");
            }
            else
            {
                string t = Uri.EscapeDataString(cfgToken);
                sb.Append("<a href=\"/panic?ui=1&t=").Append(t).Append("\" ");
                sb.Append("onclick=\"return confirm('Zerar TODAS as posicoes, cancelar ordens e desligar estrategias?')\" ");
                sb.Append("style=\"display:inline-block;background:#FF5252;color:#3b0000;text-decoration:none;");
                sb.Append("padding:10px 18px;border-radius:6px;font-weight:500\">Zerar tudo</a>");

                sb.Append("<a href=\"/killstrats?ui=1&t=").Append(t).Append("\" ");
                sb.Append("onclick=\"return confirm('Desligar todas as estrategias?')\" ");
                sb.Append("style=\"display:inline-block;margin-left:10px;background:transparent;color:#e8e8ea;");
                sb.Append("border:1px solid #33333c;text-decoration:none;padding:10px 18px;border-radius:6px\">");
                sb.Append("So estrategias</a>");

                sb.Append("<p class=\"hint\">Vale para as contas do filtro acima. Confira o resultado no Control Center.</p>");
            }
            sb.Append("</div>");

            // ---- estado atual ----
            sb.Append("<div class=\"card\"><table>");
            sb.Append("<tr><th>Contrato</th><th>Ultimo</th><th>Variacao</th><th>Posicao</th></tr>");

            List<Feed> snap;
            lock (gate) { snap = feeds.ToList(); }

            if (snap.Count == 0)
            {
                sb.Append("<tr><td colspan=\"4\" class=\"dim\">Nenhum contrato assinado. Confira o nome e se o feed esta conectado.</td></tr>");
            }
            else
            {
                foreach (Feed f in snap)
                {
                    double baseline = f.PrevClose > 0 ? f.PrevClose : f.Open;
                    double pct = (baseline > 0 && f.Last > 0) ? ((f.Last - baseline) / baseline * 100.0) : 0;
                    string cls = pct > 0 ? "up" : (pct < 0 ? "dn" : "dim");

                    int netQty = 0;
                    foreach (Account a in SelectedAccounts())
                    {
                        try
                        {
                            Position p = null;
                            lock (a.Positions)
                            {
                                p = a.Positions.FirstOrDefault(x =>
                                    x.Instrument != null && f.Instrument != null &&
                                    x.Instrument.FullName == f.Instrument.FullName);
                            }
                            if (p != null && p.MarketPosition != MarketPosition.Flat)
                                netQty += (p.MarketPosition == MarketPosition.Short) ? -p.Quantity : p.Quantity;
                        }
                        catch { }
                    }

                    sb.Append("<tr><td>").Append(Html(f.Resolved));
                    if (f.Root != f.Resolved) sb.Append(" <span class=\"dim\">(").Append(Html(f.Root)).Append(")</span>");
                    sb.Append("</td>");
                    sb.Append("<td>").Append(N(f.Last, f.Decimals)).Append("</td>");
                    sb.Append("<td class=\"").Append(cls).Append("\">").Append(N(pct, 2)).Append("%</td>");
                    sb.Append("<td>").Append(netQty == 0 ? "<span class=\"dim\">flat</span>" : netQty.ToString());
                    if (f.AlertDir != 0)
                    {
                        sb.Append(" <span class=\"dim\">alerta ").Append(f.AlertDir > 0 ? "&gt;" : "&lt;");
                        sb.Append(N(f.AlertLevel, f.Decimals)).Append("</span>");
                    }
                    sb.Append("</td></tr>");
                }
            }
            sb.Append("</table></div>");

            // diagnostico: quais contas entram na conta
            sb.Append("<div class=\"card\">");
            sb.Append("<div style=\"font-size:13px;color:#8a8a92;margin-bottom:8px\">Contas consideradas</div>");
            List<Account> selNow = SelectedAccounts();
            if (selNow.Count == 0)
            {
                sb.Append("<p class=\"dn\" style=\"margin:0\">Nenhuma. Posicoes e PnL vao aparecer vazios. ");
                sb.Append("Se voce esta testando em conta simulada, mude o filtro acima para incluir.</p>");
            }
            else
            {
                sb.Append("<table><tr><th>Conta</th><th>Posicoes</th><th>Ordens</th></tr>");
                foreach (Account a in selNow)
                {
                    int pc = 0, oc = 0;
                    try { lock (a.Positions) { foreach (Position p in a.Positions) if (p != null && p.MarketPosition != MarketPosition.Flat) pc++; } } catch { }
                    try { lock (a.Orders) { foreach (Order od in a.Orders) if (od != null && (od.OrderState == OrderState.Working || od.OrderState == OrderState.Accepted)) oc++; } } catch { }
                    sb.Append("<tr><td>").Append(Html(a.Name)).Append("</td><td>")
                      .Append(pc).Append("</td><td>").Append(oc).Append("</td></tr>");
                }
                sb.Append("</table>");
            }
            List<Account> hidden = AllAccounts().Where(a => IsIgnored(a)).ToList();
            if (hidden.Count > 0)
            {
                sb.Append("<p class=\"hint\">Ignoradas: ");
                for (int i = 0; i < hidden.Count; i++)
                {
                    if (i > 0) sb.Append(", ");
                    bool online = false;
                    try { online = (hidden[i].Connection != null); } catch { }
                    sb.Append(Html(hidden[i].Name));
                    sb.Append(online ? " <span class=\"dim\">(simulada)</span>"
                                     : " <span class=\"dim\">(desconectada)</span>");
                }
                sb.Append("</p>");
            }
            sb.Append("</div>");

            sb.Append("<div class=\"card\"><p class=\"dim\" style=\"margin:0\">Dados para o Cardputer: ");
            sb.Append("<code>/quote</code> &nbsp;|&nbsp; Config salva em <code>").Append(Html(cfgPath)).Append("</code></p></div>");

            sb.Append("<script>setTimeout(function(){location.reload()},5000)</script>");
            sb.Append("</div></body></html>");
            return sb.ToString();
        }

        private static Dictionary<string, string> ParseQuery(string path)
        {
            Dictionary<string, string> d = new Dictionary<string, string>();
            int q = path.IndexOf('?');
            if (q < 0) return d;

            foreach (string pair in path.Substring(q + 1).Split('&'))
            {
                if (pair.Length == 0) continue;
                int eq = pair.IndexOf('=');
                string k = eq > 0 ? pair.Substring(0, eq) : pair;
                string v = eq > 0 ? pair.Substring(eq + 1) : "";
                try { d[Uri.UnescapeDataString(k)] = Uri.UnescapeDataString(v.Replace("+", " ")); }
                catch { }
            }
            return d;
        }

        // =============================================================
        //  Servidor HTTP
        // =============================================================
        private void AcceptLoop()
        {
            while (running)
            {
                TcpClient client = null;
                try { client = listener.AcceptTcpClient(); }
                catch { if (!running) break; Thread.Sleep(250); continue; }

                TcpClient c = client;
                ThreadPool.QueueUserWorkItem(delegate { HandleClient(c); });
            }
        }

        private void HandleClient(TcpClient client)
        {
            try
            {
                client.ReceiveTimeout = 3000;
                client.SendTimeout = 3000;
                client.NoDelay = true;

                using (NetworkStream ns = client.GetStream())
                {
                    byte[] buf = new byte[4096];
                    int total = 0;
                    while (total < buf.Length)
                    {
                        int n = ns.Read(buf, total, buf.Length - total);
                        if (n <= 0) break;
                        total += n;
                        string sofar = Encoding.ASCII.GetString(buf, 0, total);
                        if (sofar.Contains("\r\n\r\n") || sofar.Contains("\n\n")) break;
                    }

                    string req = (total > 0) ? Encoding.ASCII.GetString(buf, 0, total) : "";
                    string full = "/";
                    int sp1 = req.IndexOf(' ');
                    if (sp1 > 0)
                    {
                        int sp2 = req.IndexOf(' ', sp1 + 1);
                        if (sp2 > sp1) full = req.Substring(sp1 + 1, sp2 - sp1 - 1);
                    }

                    Dictionary<string, string> qs = ParseQuery(full);
                    int qi = full.IndexOf('?');
                    string path = qi >= 0 ? full.Substring(0, qi) : full;

                    // ---- controle de acesso ----
                    //
                    // Regras:
                    //   comandos (panic/flatten/killstrats) -> SEMPRE exigem token
                    //   leitura e configuracao              -> exigem token, exceto
                    //                                          vindo da propria maquina
                    //
                    // A excecao do localhost existe para voce nunca se trancar
                    // fora do painel de configuracao ao definir um token.
                    bool isCommand = path.StartsWith("/panic")
                                  || path.StartsWith("/flatten")
                                  || path.StartsWith("/killstrats");

                    bool isLocal = false;
                    try
                    {
                        IPEndPoint rep = client.Client.RemoteEndPoint as IPEndPoint;
                        if (rep != null) isLocal = IPAddress.IsLoopback(rep.Address);
                    }
                    catch { }

                    if (!string.IsNullOrEmpty(cfgToken) && (isCommand || !isLocal))
                    {
                        string t;
                        if (!qs.TryGetValue("t", out t) || t != cfgToken)
                        {
                            Respond(ns, 401, "text/plain",
                                "token invalido ou ausente. Do proprio PC, use http://localhost:"
                                + cfgPort + "/ que o painel abre sem token.");
                            return;
                        }
                    }

                    if (path.StartsWith("/quote"))
                    {
                        Respond(ns, 200, "application/json", cachedJson);
                    }
                    else if (path.StartsWith("/panic") ||
                             path.StartsWith("/flatten") ||
                             path.StartsWith("/killstrats"))
                    {
                        if (string.IsNullOrEmpty(cfgToken))
                        {
                            Respond(ns, 403, "text/plain",
                                "recusado: configure um token antes de usar comandos");
                            return;
                        }

                        bool doFlatten = !path.StartsWith("/killstrats");
                        bool doKill    = !path.StartsWith("/flatten");
                        string result  = DoPanic(doFlatten, doKill);

                        bool fromBrowser = qs.ContainsKey("ui");
                        if (fromBrowser)
                        {
                            string back = isLocal ? "/" : "/?t=" + Uri.EscapeDataString(cfgToken);
                            Respond(ns, 303, "text/plain", "", "Location: " + back + "\r\n");
                        }
                        else             Respond(ns, 200, "text/plain", result);
                    }
                    else if (path.StartsWith("/ping"))
                    {
                        Respond(ns, 200, "text/plain", "pong");
                    }
                    else if (path.StartsWith("/hist"))
                    {
                        string want;
                        qs.TryGetValue("sym", out want);
                        Respond(ns, 200, "application/json", BuildHistory(want));
                    }
                    else if (path.StartsWith("/set"))
                    {
                        string v;
                        bool needRebuild = false;

                        if (qs.TryGetValue("account", out v) && v != cfgAccount)
                        {
                            cfgAccount = v;
                            Log("Conta selecionada: " + cfgAccount);
                        }
                        if (qs.TryGetValue("symbols", out v) && v.Trim().Length > 0 && v != cfgRoots)
                        {
                            cfgRoots = v.Trim();
                            needRebuild = true;
                        }
                        if (qs.TryGetValue("alerts", out v) && v != cfgAlerts)
                        {
                            cfgAlerts = v.Trim();
                            ThreadPool.QueueUserWorkItem(delegate { ApplyAlerts(); });
                        }
                        if (qs.TryGetValue("skipsim", out v))
                            cfgSkipSim = (v.ToLowerInvariant() != "false");
                        if (qs.TryGetValue("skipoff", out v))
                            cfgSkipOff = (v.ToLowerInvariant() != "false");
                        if (qs.TryGetValue("ignore", out v))
                            cfgIgnore = v.Trim();
                        if (qs.TryGetValue("token", out v) && v != cfgToken)
                        {
                            cfgToken = v.Trim();
                            Log(cfgToken.Length > 0 ? "Token definido." : "Token removido (comandos desativados).");
                        }

                        SaveConfig();
                        if (needRebuild) ThreadPool.QueueUserWorkItem(delegate { RebuildFeeds(); });

                        string back = "/";
                        if (!string.IsNullOrEmpty(cfgToken) && !isLocal)
                            back = "/?t=" + Uri.EscapeDataString(cfgToken);

                        Respond(ns, 303, "text/plain", "", "Location: " + back + "\r\n");
                    }
                    else
                    {
                        Respond(ns, 200, "text/html", BuildConfigPage());
                    }
                }
            }
            catch { }
            finally { try { client.Close(); } catch { } }
        }

        private static void Respond(NetworkStream ns, int status, string ctype, string body, string extraHeaders = "")
        {
            byte[] payload = Encoding.UTF8.GetBytes(body ?? "");
            StringBuilder head = new StringBuilder();
            head.Append("HTTP/1.1 ").Append(status).Append(' ')
                .Append(status == 200 ? "OK" : (status == 303 ? "See Other" :
                        (status == 403 ? "Forbidden" :
                        (status == 401 ? "Unauthorized" : "Error")))).Append("\r\n");
            head.Append("Content-Type: ").Append(ctype).Append("; charset=utf-8\r\n");
            head.Append("Content-Length: ").Append(payload.Length).Append("\r\n");
            head.Append("Cache-Control: no-store\r\n");
            head.Append("Access-Control-Allow-Origin: *\r\n");
            if (!string.IsNullOrEmpty(extraHeaders)) head.Append(extraHeaders);
            head.Append("Connection: close\r\n\r\n");

            byte[] hb = Encoding.ASCII.GetBytes(head.ToString());
            ns.Write(hb, 0, hb.Length);
            if (payload.Length > 0) ns.Write(payload, 0, payload.Length);
            ns.Flush();
        }
    }
}
