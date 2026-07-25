/*
 * about — orient yourself on an unfamiliar machine.
 *
 * Build native:  make
 * Build APE:     make ape   (needs cosmocc; see README)
 *
 * License: MIT — Open Shell Organization
 */

#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <unistd.h>

#ifndef ABOUT_VERSION
#define ABOUT_VERSION "0.3.0"
#endif

#define ABOUT_MAX 512
#define ABOUT_LINE 1024

struct about_info {
  char os_name[ABOUT_MAX];
  char os_version[ABOUT_MAX];
  char distro[ABOUT_MAX];
  char kernel[ABOUT_MAX];
  char arch[ABOUT_MAX];
  char hostname[ABOUT_MAX];
  char user[ABOUT_MAX];
  char home[ABOUT_MAX];
  char cwd[ABOUT_MAX];
  char shell[ABOUT_MAX];
  char shell_name[ABOUT_MAX];
  char term[ABOUT_MAX];
  char environment[ABOUT_MAX]; /* WSL2, native, container, etc. */
  char nu_version[ABOUT_MAX];
  int is_linux;
  int is_macos;
  int is_windows;
  int is_bsd;
  int is_wsl;
  int in_nushell;   /* child of a Nushell session (NU_VERSION set) */
  int nu_on_path;   /* `nu` resolvable without requiring it */
};

enum about_format {
  ABOUT_FMT_TEXT = 0,
  ABOUT_FMT_NUON = 1
};

static void trim(char *s) {
  char *end;
  while (*s && isspace((unsigned char)*s)) {
    memmove(s, s + 1, strlen(s));
  }
  end = s + strlen(s);
  while (end > s && isspace((unsigned char)end[-1])) {
    *--end = '\0';
  }
}

static void set_str(char *dst, size_t n, const char *src) {
  if (!src) {
    src = "";
  }
  snprintf(dst, n, "%s", src);
}

static int file_readable(const char *path) {
  return access(path, R_OK) == 0;
}

static int read_key_value_file(const char *path, const char *key, char *out,
                               size_t out_n) {
  FILE *f;
  char line[ABOUT_LINE];
  size_t key_len;

  f = fopen(path, "r");
  if (!f) {
    return 0;
  }
  key_len = strlen(key);
  while (fgets(line, sizeof(line), f)) {
    char *val;
    if (strncmp(line, key, key_len) != 0) {
      continue;
    }
    if (line[key_len] != '=') {
      continue;
    }
    val = line + key_len + 1;
    trim(val);
    if (val[0] == '"' || val[0] == '\'') {
      char q = val[0];
      size_t len;
      val++;
      len = strlen(val);
      if (len && val[len - 1] == q) {
        val[len - 1] = '\0';
      }
    }
    set_str(out, out_n, val);
    fclose(f);
    return 1;
  }
  fclose(f);
  return 0;
}

static int read_first_line(const char *path, char *out, size_t out_n) {
  FILE *f;
  f = fopen(path, "r");
  if (!f) {
    return 0;
  }
  if (!fgets(out, (int)out_n, f)) {
    fclose(f);
    return 0;
  }
  trim(out);
  fclose(f);
  return out[0] != '\0';
}

static int run_capture(const char *cmd, char *out, size_t out_n) {
  FILE *p;
  size_t n;
  out[0] = '\0';
  p = popen(cmd, "r");
  if (!p) {
    return 0;
  }
  n = fread(out, 1, out_n - 1, p);
  out[n] = '\0';
  pclose(p);
  trim(out);
  /* collapse newlines to "; " for multi-line tools like sw_vers */
  {
    char *s = out;
    while (*s) {
      if (*s == '\n' || *s == '\r') {
        *s = (s[1] ? ' ' : '\0');
      }
      s++;
    }
  }
  return out[0] != '\0';
}

static void basename_of(const char *path, char *out, size_t out_n) {
  const char *base = path;
  const char *p;
  if (!path || !path[0]) {
    set_str(out, out_n, "");
    return;
  }
  for (p = path; *p; p++) {
    if (*p == '/' || *p == '\\') {
      base = p + 1;
    }
  }
  set_str(out, out_n, base);
}

static int detect_wsl(void) {
  char buf[ABOUT_LINE];
  if (getenv("WSL_DISTRO_NAME") || getenv("WSL_INTEROP") ||
      getenv("WSLENV")) {
    return 1;
  }
  if (read_first_line("/proc/version", buf, sizeof(buf))) {
    if (strstr(buf, "Microsoft") || strstr(buf, "WSL") ||
        strstr(buf, "microsoft")) {
      return 1;
    }
  }
  if (read_first_line("/proc/sys/kernel/osrelease", buf, sizeof(buf))) {
    if (strstr(buf, "microsoft") || strstr(buf, "WSL")) {
      return 1;
    }
  }
  return 0;
}

static int detect_container(void) {
  if (file_readable("/.dockerenv")) {
    return 1;
  }
  if (file_readable("/run/.containerenv")) {
    return 1;
  }
  if (getenv("container") || getenv("KUBERNETES_SERVICE_HOST")) {
    return 1;
  }
  return 0;
}

static void detect_linux_distro(struct about_info *info) {
  char pretty[ABOUT_MAX];
  char name[ABOUT_MAX];
  char version[ABOUT_MAX];
  pretty[0] = name[0] = version[0] = '\0';

  if (file_readable("/etc/os-release")) {
    read_key_value_file("/etc/os-release", "PRETTY_NAME", pretty,
                        sizeof(pretty));
    read_key_value_file("/etc/os-release", "NAME", name, sizeof(name));
    read_key_value_file("/etc/os-release", "VERSION", version, sizeof(version));
    if (!version[0]) {
      read_key_value_file("/etc/os-release", "VERSION_ID", version,
                          sizeof(version));
    }
  } else if (file_readable("/usr/lib/os-release")) {
    read_key_value_file("/usr/lib/os-release", "PRETTY_NAME", pretty,
                        sizeof(pretty));
    read_key_value_file("/usr/lib/os-release", "NAME", name, sizeof(name));
    read_key_value_file("/usr/lib/os-release", "VERSION", version,
                        sizeof(version));
  }

  if (pretty[0]) {
    set_str(info->distro, sizeof(info->distro), pretty);
  } else if (name[0]) {
    if (version[0]) {
      snprintf(info->distro, sizeof(info->distro), "%s %s", name, version);
    } else {
      set_str(info->distro, sizeof(info->distro), name);
    }
  } else if (file_readable("/etc/redhat-release")) {
    read_first_line("/etc/redhat-release", info->distro, sizeof(info->distro));
  } else if (file_readable("/etc/debian_version")) {
    char ver[64];
    if (read_first_line("/etc/debian_version", ver, sizeof(ver))) {
      snprintf(info->distro, sizeof(info->distro), "Debian %s", ver);
    }
  }

  if (version[0]) {
    set_str(info->os_version, sizeof(info->os_version), version);
  }
}

static void detect_macos(struct about_info *info) {
  char buf[ABOUT_MAX];
  set_str(info->os_name, sizeof(info->os_name), "macOS");
  if (run_capture("sw_vers -productVersion 2>/dev/null", buf, sizeof(buf))) {
    set_str(info->os_version, sizeof(info->os_version), buf);
    set_str(info->distro, sizeof(info->distro), "macOS ");
    /* append version without risking truncation warnings on huge buffers */
    {
      size_t used = strlen(info->distro);
      snprintf(info->distro + used, sizeof(info->distro) - used, "%s", buf);
    }
  } else {
    set_str(info->distro, sizeof(info->distro), "macOS");
  }
  if (run_capture("sw_vers -productName 2>/dev/null", buf, sizeof(buf))) {
    set_str(info->os_name, sizeof(info->os_name), buf);
  }
}

static void detect_windows(struct about_info *info) {
  char buf[ABOUT_MAX];
  const char *os = getenv("OS");
  set_str(info->os_name, sizeof(info->os_name), "Windows");
  if (os && *os) {
    set_str(info->distro, sizeof(info->distro), os);
  } else {
    set_str(info->distro, sizeof(info->distro), "Windows");
  }
  /* Best-effort version via cmd (works under Cosmopolitan on Windows). */
  if (run_capture("cmd.exe /c ver 2>nul", buf, sizeof(buf)) ||
      run_capture("ver 2>/dev/null", buf, sizeof(buf))) {
    set_str(info->os_version, sizeof(info->os_version), buf);
    set_str(info->distro, sizeof(info->distro), buf);
  }
}

static int probe_nu_on_path(void) {
  char buf[ABOUT_MAX];
  /* Prefer POSIX which/command; fall back to Windows where.exe. */
  if (run_capture("command -v nu 2>/dev/null", buf, sizeof(buf))) {
    return 1;
  }
  if (run_capture("which nu 2>/dev/null", buf, sizeof(buf))) {
    return 1;
  }
  if (run_capture("where.exe nu 2>nul", buf, sizeof(buf))) {
    return 1;
  }
  return 0;
}

static void detect_shell(struct about_info *info) {
  const char *shell = getenv("SHELL");
  const char *comspec;
  const char *psmod;
  const char *nu_ver = getenv("NU_VERSION");

  if (nu_ver && nu_ver[0]) {
    info->in_nushell = 1;
    set_str(info->nu_version, sizeof(info->nu_version), nu_ver);
  }

  info->nu_on_path = info->in_nushell || probe_nu_on_path();

  if (info->in_nushell) {
    /* Prefer the live session over $SHELL (often still bash/zsh login shell). */
    set_str(info->shell, sizeof(info->shell), "nu ");
    {
      size_t used = strlen(info->shell);
      snprintf(info->shell + used, sizeof(info->shell) - used, "%s",
               info->nu_version);
    }
    set_str(info->shell_name, sizeof(info->shell_name), "nu");
    return;
  }

  if (shell && shell[0]) {
    set_str(info->shell, sizeof(info->shell), shell);
  } else {
    /* Windows native shells */
    psmod = getenv("PSModulePath");
    comspec = getenv("ComSpec");
    if (!comspec) {
      comspec = getenv("COMSPEC");
    }
    if (psmod && *psmod) {
      /* Likely PowerShell session */
      if (getenv("POWERSHELL_DISTRIBUTION_CHANNEL") ||
          getenv("PSVersionTable") /* rarely exported */) {
        set_str(info->shell, sizeof(info->shell), "pwsh/powershell");
      } else if (comspec && *comspec) {
        set_str(info->shell, sizeof(info->shell), comspec);
      } else {
        set_str(info->shell, sizeof(info->shell), "powershell?");
      }
    } else if (comspec && *comspec) {
      set_str(info->shell, sizeof(info->shell), comspec);
    } else {
      struct passwd *pw = getpwuid(getuid());
      if (pw && pw->pw_shell) {
        set_str(info->shell, sizeof(info->shell), pw->pw_shell);
      } else {
        set_str(info->shell, sizeof(info->shell), "(unknown)");
      }
    }
  }

  basename_of(info->shell, info->shell_name, sizeof(info->shell_name));
  if (!info->shell_name[0]) {
    set_str(info->shell_name, sizeof(info->shell_name), info->shell);
  }

  /* Refine PowerShell detection when SHELL is unset but we are in pwsh */
  if ((!shell || !shell[0]) && getenv("PSModulePath")) {
    if (strstr(info->shell_name, "cmd") || strstr(info->shell, "cmd.exe")) {
      set_str(info->shell_name, sizeof(info->shell_name), "powershell");
      /* Keep ComSpec path in shell; name field carries the session hint. */
    }
  }
}

static void gather(struct about_info *info) {
  struct utsname uts;
  const char *user;
  const char *home;
  const char *term;
  char cwd[ABOUT_MAX];
  int have_uname;

  memset(info, 0, sizeof(*info));
  memset(&uts, 0, sizeof(uts));

  have_uname = (uname(&uts) == 0);
  if (have_uname) {
    set_str(info->arch, sizeof(info->arch), uts.machine);
    set_str(info->os_name, sizeof(info->os_name), uts.sysname);
    {
      char full[ABOUT_MAX];
      snprintf(full, sizeof(full), "%s %s", uts.sysname, uts.release);
      set_str(info->kernel, sizeof(info->kernel), full);
    }
  }

  if (gethostname(info->hostname, sizeof(info->hostname)) != 0) {
    const char *hn = getenv("HOSTNAME");
    if (!hn) {
      hn = getenv("COMPUTERNAME");
    }
    set_str(info->hostname, sizeof(info->hostname), hn ? hn : "(unknown)");
  }

  user = getenv("USER");
  if (!user || !user[0]) {
    user = getenv("USERNAME");
  }
  if (!user || !user[0]) {
    user = getenv("LOGNAME");
  }
  if (user && user[0]) {
    set_str(info->user, sizeof(info->user), user);
  } else {
    struct passwd *pw = getpwuid(getuid());
    set_str(info->user, sizeof(info->user),
            (pw && pw->pw_name) ? pw->pw_name : "(unknown)");
  }

  home = getenv("HOME");
  if (!home || !home[0]) {
    home = getenv("USERPROFILE");
  }
  set_str(info->home, sizeof(info->home), home ? home : "(unknown)");

  if (getcwd(cwd, sizeof(cwd))) {
    set_str(info->cwd, sizeof(info->cwd), cwd);
  } else {
    set_str(info->cwd, sizeof(info->cwd), "(unknown)");
  }

  term = getenv("TERM_PROGRAM");
  if (!term || !term[0]) {
    term = getenv("TERM");
  }
  if (!term || !term[0]) {
    if (getenv("WT_SESSION")) {
      term = "Windows Terminal";
    } else {
      term = "(unknown)";
    }
  }
  set_str(info->term, sizeof(info->term), term);

  detect_shell(info);

  /* Platform family */
  if (have_uname && uts.sysname[0]) {
    if (strcmp(uts.sysname, "Linux") == 0) {
      info->is_linux = 1;
    } else if (strcmp(uts.sysname, "Darwin") == 0) {
      info->is_macos = 1;
    } else if (strncmp(uts.sysname, "CYGWIN", 6) == 0 ||
               strncmp(uts.sysname, "MINGW", 5) == 0 ||
               strncmp(uts.sysname, "MSYS", 4) == 0 ||
               strcmp(uts.sysname, "Windows_NT") == 0 ||
               strcmp(uts.sysname, "Windows") == 0) {
      info->is_windows = 1;
    } else if (strstr(uts.sysname, "BSD")) {
      info->is_bsd = 1;
    }
  }

  /* Cosmopolitan / Windows without useful uname */
  if (!info->is_linux && !info->is_macos && !info->is_bsd) {
    const char *os = getenv("OS");
    if ((os && strcmp(os, "Windows_NT") == 0) || getenv("USERPROFILE") ||
        getenv("SystemRoot") || getenv("windir")) {
      if (!file_readable("/etc/os-release") && !file_readable("/proc/version")) {
        info->is_windows = 1;
      }
    }
  }

  info->is_wsl = info->is_linux && detect_wsl();

  if (info->is_linux) {
    detect_linux_distro(info);
    if (!info->distro[0]) {
      set_str(info->distro, sizeof(info->distro), "Linux");
    }
    if (info->is_wsl) {
      const char *distro = getenv("WSL_DISTRO_NAME");
      if (distro && *distro) {
        snprintf(info->environment, sizeof(info->environment),
                 "WSL2 (%s on Windows)", distro);
      } else {
        set_str(info->environment, sizeof(info->environment),
                "WSL2 (Windows Subsystem for Linux)");
      }
    } else if (detect_container()) {
      set_str(info->environment, sizeof(info->environment),
              "container / orchestrated host");
    } else {
      set_str(info->environment, sizeof(info->environment), "native Linux");
    }
  } else if (info->is_macos) {
    detect_macos(info);
    set_str(info->environment, sizeof(info->environment), "native macOS");
  } else if (info->is_windows) {
    detect_windows(info);
    set_str(info->environment, sizeof(info->environment), "native Windows");
  } else if (info->is_bsd) {
    snprintf(info->distro, sizeof(info->distro), "%s", info->os_name);
    set_str(info->environment, sizeof(info->environment), "native BSD");
  } else {
    set_str(info->distro, sizeof(info->distro),
            info->os_name[0] ? info->os_name : "(unknown OS)");
    set_str(info->environment, sizeof(info->environment), "(unknown)");
  }
}

static void print_field(const char *label, const char *value) {
  printf("  %-14s %s\n", label, value && value[0] ? value : "(unknown)");
}

static void print_nuon_string(const char *s) {
  putchar('"');
  if (!s) {
    putchar('"');
    return;
  }
  for (; *s; s++) {
    unsigned char c = (unsigned char)*s;
    if (c == '\\' || c == '"') {
      putchar('\\');
      putchar((char)c);
    } else if (c == '\n') {
      fputs("\\n", stdout);
    } else if (c == '\r') {
      fputs("\\r", stdout);
    } else if (c == '\t') {
      fputs("\\t", stdout);
    } else {
      putchar((char)c);
    }
  }
  putchar('"');
}

static void print_nuon_field(const char *key, const char *value, int *first) {
  if (!*first) {
    fputs(",\n", stdout);
  }
  *first = 0;
  printf("  %s: ", key);
  print_nuon_string(value && value[0] ? value : "(unknown)");
}

static void print_nuon(const struct about_info *info) {
  int first = 1;
  puts("{");
  print_nuon_field("distro", info->distro, &first);
  print_nuon_field("kernel", info->kernel, &first);
  print_nuon_field("environment", info->environment, &first);
  print_nuon_field("arch", info->arch, &first);
  print_nuon_field("hostname", info->hostname, &first);
  print_nuon_field("user", info->user, &first);
  print_nuon_field("shell", info->shell, &first);
  print_nuon_field("shell_name", info->shell_name, &first);
  print_nuon_field("home", info->home, &first);
  print_nuon_field("cwd", info->cwd, &first);
  print_nuon_field("terminal", info->term, &first);
  print_nuon_field("about", ABOUT_VERSION, &first);
  if (info->in_nushell) {
    print_nuon_field("nushell", info->nu_version, &first);
  }
  if (!first) {
    fputs(",\n", stdout);
  }
  printf("  in_nushell: %s\n", info->in_nushell ? "true" : "false");
  puts("}");
}

static void print_row(const char *label, const char *value) {
  char cell[45];
  const char *v = value && value[0] ? value : "(unknown)";
  size_t len = strlen(v);
  if (len > 44) {
    memcpy(cell, v, 41);
    cell[41] = '.';
    cell[42] = '.';
    cell[43] = '.';
    cell[44] = '\0';
    v = cell;
  }
  printf("│ %-14s │ %-44s │\n", label, v);
}

static void print_report_nushell(const struct about_info *info) {
  puts("");
  puts("about — where am I?  (Nushell session)");
  puts("╭────────────────┬──────────────────────────────────────────────╮");
  print_row("OS / Distro", info->distro);
  print_row("Kernel", info->kernel);
  print_row("Environment", info->environment);
  print_row("Arch", info->arch);
  print_row("Hostname", info->hostname);
  print_row("User", info->user);
  print_row("Shell", info->shell);
  print_row("Home", info->home);
  print_row("CWD", info->cwd);
  print_row("Terminal", info->term);
  print_row("about", ABOUT_VERSION);
  puts("╰────────────────┴──────────────────────────────────────────────╯");
}

static void print_tips_nushell(const struct about_info *info) {
  puts("");
  puts("Look around (Nushell — structured, cross-platform)");
  puts("────────────────────────────────────────");
  puts("  sys                         # host / cpu / mem / disks / net");
  puts("  sys host                    # hostname, OS family, kernel");
  puts("  sys disk                    # volumes (structured)");
  puts("  sys mem                     # memory");
  puts("  version                     # this Nushell build");
  puts("  $nu.os-info                 # OS name / arch / family");
  puts("  $env | sort                 # environment as a table");
  puts("  ls /                        # filesystem roots (records)");
  puts("  df                          # disk free space");
  puts("  ps                          # processes as a table");
  puts("  which apt dnf zypper brew winget   # package managers on PATH");
  puts("  help commands | where name =~ 'sys'  # discover related builtins");
  puts("  about --format nuon | from nuon      # machine-readable about");
  puts("  about tar                    # short orientation card for a command");
  puts("  help tar                     # progressive help (prohelp), if installed");
  puts("  info tar                     # Info manual (OpenShellOrg / GNU)");
  puts("  man tar                      # traditional man page");
  if (info->is_wsl) {
    puts("  ^wsl.exe -l -v              # list WSL distros (external)");
    puts("  $env.WSL_DISTRO_NAME?       # this distro's name (if set)");
  }
  if (info->is_windows && !info->is_wsl) {
    puts("  ^wsl.exe -l -v              # Linux distros on this Windows host");
  }
  puts("");
  puts("You're already out of the text-soup shells. Stay in `nu`.");
  puts("");
}

static void print_nushell_nudge(const struct about_info *info) {
  puts("Escape hatch: Nushell");
  puts("────────────────────────────────────────");
  puts("  You're in an old-fashioned shell — flags, pipes of text, and");
  puts("  tribal knowledge just to answer \"what machine is this?\"");
  puts("");
  puts("  Nushell gives the same answers as structured tables:");
  puts("    sys host / sys disk / $env / ls / df / ps");
  puts("");
  puts("  Install:  https://www.nushell.sh/");
  puts("    winget install Nushell.Nushell     # Windows");
  puts("    brew install nushell               # macOS / Linuxbrew");
  puts("    cargo install nu                   # anywhere with Rust");
  puts("    # or your distro package (apt, dnf, pacman, …)");
  puts("");
  if (info->nu_on_path) {
    puts("  `nu` is already on PATH — run:  nu");
    puts("  Then re-run `about` for a Nushell-native cheat sheet.");
  } else {
    puts("  After install, run:  nu");
    puts("  Then re-run `about` inside Nushell for the good tips.");
  }
  puts("");
}

static void print_tips_linux(int is_wsl) {
  puts("  cat /etc/os-release          # distro identity (PRETTY_NAME, ID)");
  puts("  uname -a                     # kernel, arch, hostname");
  puts("  echo \"$SHELL\" && ps -p $$    # login shell vs current process");
  puts("  hostnamectl                  # systemd hosts (if available)");
  puts("  df -h                        # disk space");
  puts("  free -h                      # memory");
  puts("  ip -br a                     # network addresses (or: ip a)");
  puts("  env | sort                   # environment variables");
  puts("  ls /                         # filesystem layout");
  puts("  command -v apt dnf zypper pacman apk  # which package manager");
  puts("  about tar                    # short orientation for a command");
  puts("  help tar                     # progressive help (prohelp)");
  puts("  info tar                     # Info manual");
  puts("  man tar                      # man page");
  if (is_wsl) {
    puts("  wsl.exe -l -v                # from Windows: list distros");
    puts("  echo \"$WSL_DISTRO_NAME\"      # this WSL distro's name");
  }
}

static void print_tips_macos(void) {
  puts("  sw_vers                      # macOS product name / version");
  puts("  uname -a                     # Darwin kernel + arch");
  puts("  echo \"$SHELL\" && ps -p $$    # login shell vs current process");
  puts("  system_profiler SPSoftwareDataType   # deeper software inventory");
  puts("  df -h                        # disk space");
  puts("  vm_stat                      # memory pressure (pages)");
  puts("  ifconfig                     # network interfaces");
  puts("  brew --prefix                # Homebrew prefix (if installed)");
  puts("  env | sort                   # environment variables");
  puts("  about tar                    # short orientation for a command");
  puts("  help tar / info tar / man tar");
}

static void print_tips_windows(void) {
  puts("  ver                          # Windows version string");
  puts("  systeminfo                   # full system summary (slow)");
  puts("  echo %OS% & echo %ComSpec%   # OS + default cmd interpreter");
  puts("  echo $PSVersionTable         # PowerShell version (in pwsh)");
  puts("  Get-ComputerInfo             # rich inventory (PowerShell)");
  puts("  wsl -l -v                    # installed Linux distros (WSL)");
  puts("  where.exe about              # which about is on PATH");
  puts("  Get-ChildItem Env: | Sort-Object Name   # environment");
  puts("  about tar                    # short orientation for a command");
  puts("  help tar / info tar / man tar");
}

static void print_tips_bsd(void) {
  puts("  uname -a                     # kernel + arch");
  puts("  cat /etc/os-release          # if present");
  puts("  echo \"$SHELL\" && ps -p $$    # shell");
  puts("  df -h                        # disk space");
  puts("  sysctl -a | less             # kernel tunables (careful)");
  puts("  ifconfig                     # network");
  puts("  about tar                    # short orientation for a command");
  puts("  help tar / info tar / man tar");
}

static void print_tips_classic(const struct about_info *info) {
  puts("");
  puts("Look around (common discovery commands for this shell)");
  puts("────────────────────────────────────────");
  if (info->is_linux) {
    print_tips_linux(info->is_wsl);
  } else if (info->is_macos) {
    print_tips_macos();
  } else if (info->is_windows) {
    print_tips_windows();
  } else if (info->is_bsd) {
    print_tips_bsd();
  } else {
    puts("  uname -a");
    puts("  echo \"$SHELL\"");
    puts("  env | sort");
  }
  puts("");
}

static int extract_sdl_field(const char *sdl, const char *key, char *out, size_t out_n) {
  char pattern[128];
  const char *p;
  const char *start;
  const char *end;
  size_t key_len;

  snprintf(pattern, sizeof(pattern), "%s \"", key);
  p = strstr(sdl, pattern);
  if (!p) {
    return 0;
  }
  start = p + strlen(pattern);
  end = start;
  while (*end && *end != '"') {
    if (*end == '\\' && end[1]) {
      end += 2;
      continue;
    }
    end++;
  }
  if (*end != '"') {
    return 0;
  }
  key_len = (size_t)(end - start);
  if (key_len >= out_n) {
    key_len = out_n - 1;
  }
  memcpy(out, start, key_len);
  out[key_len] = '\0';
  return 1;
}

static int tool_on_path(const char *name) {
  char cmd[ABOUT_LINE];
  char out[ABOUT_MAX];
#if defined(_WIN32) && !defined(__CYGWIN__)
  snprintf(cmd, sizeof(cmd), "where %s 2>NUL", name);
#else
  snprintf(cmd, sizeof(cmd), "command -v %s 2>/dev/null", name);
#endif
  return run_capture(cmd, out, sizeof(out)) && out[0] != '\0';
}

static void list_binaries(const char *name) {
  char cmd[ABOUT_LINE];
  char out[ABOUT_LINE * 4];
#if defined(_WIN32) && !defined(__CYGWIN__)
  snprintf(cmd, sizeof(cmd), "where %s 2>NUL", name);
#else
  snprintf(cmd, sizeof(cmd), "which -a %s 2>/dev/null || command -v %s 2>/dev/null",
           name, name);
#endif
  if (run_capture(cmd, out, sizeof(out)) && out[0]) {
    char *line = out;
    char *nl;
    puts("Binaries:");
    while (line && *line) {
      nl = strchr(line, '\n');
      if (nl) {
        *nl = '\0';
      }
      if (line[0]) {
        printf("  %s\n", line);
      }
      line = nl ? nl + 1 : NULL;
    }
  } else {
    puts("Binaries: (not found on PATH)");
  }
}

static int load_orient_via(const char *tool, const char *name, char *buf, size_t buf_n) {
  char cmd[ABOUT_LINE];
#if defined(_WIN32) && !defined(__CYGWIN__)
  snprintf(cmd, sizeof(cmd), "%s --orient %s 2>NUL", tool, name);
#else
  snprintf(cmd, sizeof(cmd), "%s --orient %s 2>/dev/null", tool, name);
#endif
  return run_capture(cmd, buf, buf_n) && strstr(buf, "orientation") != NULL;
}

static void print_command_card(const char *name) {
  char sdl[ABOUT_LINE * 8];
  char summary[ABOUT_MAX];
  char description[ABOUT_LINE * 2];
  char history[ABOUT_LINE * 2];
  int have = 0;

  summary[0] = description[0] = history[0] = '\0';
  sdl[0] = '\0';

  if (tool_on_path("info") && load_orient_via("info", name, sdl, sizeof(sdl))) {
    have = 1;
  } else if (tool_on_path("help") && load_orient_via("help", name, sdl, sizeof(sdl))) {
    have = 1;
  }

  if (have) {
    extract_sdl_field(sdl, "summary", summary, sizeof(summary));
    extract_sdl_field(sdl, "description", description, sizeof(description));
    extract_sdl_field(sdl, "history", history, sizeof(history));
  }

  puts("");
  printf("%s — orientation\n", name);
  puts("────────────────────────────────────────");
  if (summary[0]) {
    puts(summary);
  } else {
    puts("(No shared orientation pack yet — dig deeper with the links below.)");
  }
  if (description[0]) {
    puts("");
    puts(description);
  }
  if (history[0]) {
    puts("");
    puts("Background");
    puts(history);
  }
  puts("");
  list_binaries(name);
  puts("");
  puts("Dig deeper:");
  printf("  help %s                 # progressive help (prohelp)%s\n", name,
         tool_on_path("help") ? "" : "  [not on PATH]");
  printf("  info %s                 # Info manual%s\n", name,
         tool_on_path("info") ? "" : "  [not on PATH]");
  printf("  man %s                  # man page%s\n", name,
         tool_on_path("man") ? "" : "  [not on PATH]");
  puts("");
}

static void print_report_classic(const struct about_info *info) {
  puts("");
  puts("about — where am I?");
  puts("────────────────────────────────────────");
  print_field("OS / Distro:", info->distro);
  print_field("Kernel:", info->kernel);
  print_field("Environment:", info->environment);
  print_field("Arch:", info->arch);
  print_field("Hostname:", info->hostname);
  print_field("User:", info->user);
  print_field("Shell:", info->shell);
  if (info->shell_name[0] && strcmp(info->shell_name, info->shell) != 0) {
    print_field("Shell name:", info->shell_name);
  }
  print_field("Home:", info->home);
  print_field("CWD:", info->cwd);
  print_field("Terminal:", info->term);
  printf("  %-14s %s\n", "about:", ABOUT_VERSION);
  puts("────────────────────────────────────────");
}

static void print_report(const struct about_info *info, int tips,
                         int use_nu_tips) {
  if (info->in_nushell) {
    print_report_nushell(info);
  } else {
    print_report_classic(info);
  }

  if (!tips) {
    puts("  (tips omitted; pass --tips or run without --quiet)");
    puts("");
    return;
  }

  if (use_nu_tips) {
    print_tips_nushell(info);
  } else {
    print_tips_classic(info);
    print_nushell_nudge(info);
    puts("Tip: install this binary on PATH as `about` so every shell");
    puts("     can answer \"what machine is this?\" in one word.");
    puts("");
  }
}

static void print_usage(const char *argv0) {
  printf("Usage: %s [options]\n", argv0);
  printf("       %s <command>\n\n", argv0);
  puts("Orient yourself: distro/OS, shell, and how to explore further.");
  puts("Or: short orientation card for a command (not a full manual).");
  puts("Works in any shell; under Nushell, tips become structured `nu` idioms.");
  puts("");
  puts("Options:");
  puts("  -h, --help           Show this help");
  puts("  -v, --version        Print version");
  puts("  -q, --quiet          Facts only (no discovery tips / nudge)");
  puts("  -t, --tips           Tips only (skip the facts header)");
  puts("  --format text|nuon   Output format (default: text)");
  puts("  --nu                 Force Nushell tip sheet (even outside nu)");
  puts("  --classic            Force classic UNIX/PowerShell tip sheet");
  puts("");
  puts("Command card:");
  puts("  about tar            Summary / background + dig-deeper links");
  puts("");
}

int main(int argc, char **argv) {
  struct about_info info;
  int tips = 1;
  int tips_only = 0;
  int force_nu = 0;
  int force_classic = 0;
  enum about_format fmt = ABOUT_FMT_TEXT;
  const char *command_card = NULL;
  int i;

  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      print_usage(argv[0]);
      return 0;
    }
    if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
      printf("about %s\n", ABOUT_VERSION);
      return 0;
    }
    if (strcmp(argv[i], "-q") == 0 || strcmp(argv[i], "--quiet") == 0) {
      tips = 0;
      continue;
    }
    if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--tips") == 0) {
      tips_only = 1;
      continue;
    }
    if (strcmp(argv[i], "--nu") == 0) {
      force_nu = 1;
      continue;
    }
    if (strcmp(argv[i], "--classic") == 0) {
      force_classic = 1;
      continue;
    }
    if (strcmp(argv[i], "--format") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "about: --format needs text or nuon\n");
        return 2;
      }
      i++;
      if (strcmp(argv[i], "text") == 0) {
        fmt = ABOUT_FMT_TEXT;
      } else if (strcmp(argv[i], "nuon") == 0) {
        fmt = ABOUT_FMT_NUON;
      } else {
        fprintf(stderr, "about: unknown format: %s (use text or nuon)\n",
                argv[i]);
        return 2;
      }
      continue;
    }
    if (strncmp(argv[i], "--format=", 9) == 0) {
      const char *f = argv[i] + 9;
      if (strcmp(f, "text") == 0) {
        fmt = ABOUT_FMT_TEXT;
      } else if (strcmp(f, "nuon") == 0) {
        fmt = ABOUT_FMT_NUON;
      } else {
        fprintf(stderr, "about: unknown format: %s (use text or nuon)\n", f);
        return 2;
      }
      continue;
    }
    if (argv[i][0] == '-') {
      fprintf(stderr, "about: unknown option: %s\n", argv[i]);
      print_usage(argv[0]);
      return 2;
    }
    if (command_card) {
      fprintf(stderr, "about: unexpected argument: %s\n", argv[i]);
      print_usage(argv[0]);
      return 2;
    }
    command_card = argv[i];
  }

  if (command_card) {
    if (tips_only || force_nu || force_classic || fmt == ABOUT_FMT_NUON) {
      fprintf(stderr,
              "about: <command> card cannot combine with --tips/--nu/--classic/--format\n");
      return 2;
    }
    print_command_card(command_card);
    return 0;
  }

  if (force_nu && force_classic) {
    fprintf(stderr, "about: --nu and --classic are mutually exclusive\n");
    return 2;
  }

  gather(&info);

  {
    int use_nu_tips = info.in_nushell;
    if (force_nu) {
      use_nu_tips = 1;
    } else if (force_classic) {
      use_nu_tips = 0;
    }

    if (fmt == ABOUT_FMT_NUON) {
      print_nuon(&info);
      return 0;
    }

    if (tips_only) {
      if (use_nu_tips) {
        print_tips_nushell(&info);
      } else {
        print_tips_classic(&info);
        print_nushell_nudge(&info);
      }
      return 0;
    }

    print_report(&info, tips, use_nu_tips);
    return 0;
  }
}
