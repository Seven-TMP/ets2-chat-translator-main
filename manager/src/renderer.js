const els = {
  statusPill: document.querySelector('#statusPill'),
  installState: document.querySelector('#installState'),
  pluginDir: document.querySelector('#pluginDir'),
  ets2Path: document.querySelector('#ets2Path'),
  detectBtn: document.querySelector('#detectBtn'),
  browseBtn: document.querySelector('#browseBtn'),
  installBtn: document.querySelector('#installBtn'),
  uninstallBtn: document.querySelector('#uninstallBtn'),
  preset: document.querySelector('#preset'),
  targetLang: document.querySelector('#targetLang'),
  workers: document.querySelector('#workers'),
  queueLimit: document.querySelector('#queueLimit'),
  cacheLimit: document.querySelector('#cacheLimit'),
  timeoutMs: document.querySelector('#timeoutMs'),
  fontSize: document.querySelector('#fontSize'),
  fontSizeValue: document.querySelector('#fontSizeValue'),
  fontSizeMinus: document.querySelector('#fontSizeMinus'),
  fontSizePlus: document.querySelector('#fontSizePlus'),
  llmFields: document.querySelector('#llmFields'),
  providerLabel: document.querySelector('#providerLabel'),
  baseUrl: document.querySelector('#baseUrl'),
  apiKey: document.querySelector('#apiKey'),
  apiSecret: document.querySelector('#apiSecret'),
  model: document.querySelector('#model'),
  sourceLang: document.querySelector('#sourceLang'),
  enableFallbacks: document.querySelector('#enableFallbacks'),
  loadConfigBtn: document.querySelector('#loadConfigBtn'),
  previewBtn: document.querySelector('#previewBtn'),
  saveConfigBtn: document.querySelector('#saveConfigBtn'),
  preview: document.querySelector('#preview')
};

function numberValue(input, fallback) {
  const value = Number.parseInt(input.value, 10);
  return Number.isFinite(value) ? value : fallback;
}

const PROVIDER_PRESETS = {
  'anthropic': {
    kind: 'anthropic',
    label: 'Anthropic',
    baseUrl: 'https://api.anthropic.com/v1',
    model: 'claude-3-5-haiku-latest',
    needsModel: true,
    needsApiKey: true
  },
  'openai': {
    kind: 'openai_compatible',
    label: 'OpenAI',
    baseUrl: 'https://api.openai.com/v1',
    model: 'gpt-4o-mini',
    needsModel: true,
    needsApiKey: true
  },
  'deepl': {
    kind: 'deepl',
    label: 'DeepL',
    baseUrl: 'https://api-free.deepl.com',
    model: '',
    needsModel: false,
    needsApiKey: true
  },
  'google-cloud': {
    kind: 'google_cloud',
    label: 'Google Cloud Translate',
    baseUrl: 'https://translation.googleapis.com',
    model: '',
    needsModel: false,
    needsApiKey: true
  },
  'microsoft': {
    kind: 'microsoft',
    label: 'Microsoft Translator',
    baseUrl: 'https://api.cognitive.microsofttranslator.com',
    model: '',
    needsModel: false,
    needsApiKey: true,
    needsSecret: true,
    secretLabel: 'Region'
  },
  'baidu': {
    kind: 'baidu',
    label: '百度翻译',
    baseUrl: 'https://fanyi-api.baidu.com',
    model: '',
    needsModel: false,
    needsApiKey: true,
    needsSecret: true
  },
  'youdao': {
    kind: 'youdao',
    label: '有道智云',
    baseUrl: 'https://openapi.youdao.com',
    model: '',
    needsModel: false,
    needsApiKey: true,
    needsSecret: true
  },
  'tencent': {
    kind: 'tencent',
    label: '腾讯云机器翻译',
    baseUrl: 'https://tmt.tencentcloudapi.com',
    model: 'ap-guangzhou',
    needsModel: true,
    modelLabel: 'Region',
    needsApiKey: true,
    needsSecret: true
  },
  'libretranslate': {
    kind: 'libretranslate',
    label: 'LibreTranslate',
    baseUrl: 'https://libretranslate.com',
    model: '',
    needsModel: false,
    needsApiKey: false
  },
  'free': {
    kind: '',
    label: '',
    baseUrl: '',
    model: '',
    needsModel: false,
    needsApiKey: false
  }
};

function fallbackProviders(target) {
  return [
    { kind: 'mymemory', label: 'MyMemory', enabled: true, source: 'auto', target },
    { kind: 'andeer', label: 'Andeer', enabled: false, source: 'auto', target }
  ];
}

function baseConfig() {
  const target = els.targetLang.value.trim() || 'zh-CN';
  return {
    target_lang: target,
    workers: Math.max(1, Math.min(32, numberValue(els.workers, 8))),
    queue_limit: Math.max(50, numberValue(els.queueLimit, 1000)),
    cache_limit: Math.max(100, numberValue(els.cacheLimit, 1500)),
    timeout_ms: Math.max(3000, numberValue(els.timeoutMs, 10000)),
    font_size: Math.max(12, Math.min(28, numberValue(els.fontSize, 18))),
    providers: []
  };
}

function buildConfig() {
  const config = baseConfig();
  const target = config.target_lang;
  const selected = PROVIDER_PRESETS[els.preset.value] || PROVIDER_PRESETS.free;
  if (selected.kind) {
    config.providers.unshift({
      kind: selected.kind,
      label: els.providerLabel.value.trim() || selected.label,
      enabled: true,
      base_url: els.baseUrl.value.trim(),
      api_key: els.apiKey.value,
      api_secret: els.apiSecret.value,
      model: selected.needsModel ? (els.model.value.trim() || selected.model) : '',
      source: els.sourceLang.value.trim() || 'auto',
      target
    });
  }
  if (els.enableFallbacks.checked || !selected.kind) {
    config.providers.push(...fallbackProviders(target));
  }
  return config;
}

function setStatus(text) {
  els.statusPill.textContent = text;
}

function updatePreview() {
  const selected = PROVIDER_PRESETS[els.preset.value] || PROVIDER_PRESETS.free;
  els.llmFields.style.display = selected.kind ? 'grid' : 'none';
  els.model.closest('.form-group').style.display = selected.needsModel ? 'flex' : 'none';
  els.apiSecret.closest('.form-group').style.display = selected.needsSecret ? 'flex' : 'none';
  els.apiSecret.placeholder = selected.secretLabel || '签名平台需要';
  els.model.closest('.form-group').querySelector('label').textContent = selected.modelLabel || '模型';
  els.apiKey.placeholder = selected.needsApiKey ? '必填' : '可选';
  els.preview.value = JSON.stringify(buildConfig(), null, 2);
}

function applyPresetDefaults(force = false) {
  const selected = PROVIDER_PRESETS[els.preset.value] || PROVIDER_PRESETS.free;
  if (!selected.kind) {
    updatePreview();
    return;
  }
  if (force || !els.providerLabel.value.trim()) els.providerLabel.value = selected.label;
  if (force || !els.baseUrl.value.trim()) els.baseUrl.value = selected.baseUrl;
  if (force || !els.model.value.trim()) els.model.value = selected.model;
  if (!els.sourceLang.value.trim()) els.sourceLang.value = 'auto';
  updatePreview();
}

async function refreshState() {
  const state = await window.managerApi.state(els.ets2Path.value.trim());
  els.installState.textContent = state.text;
  els.pluginDir.textContent = state.pluginDir ? `插件目录：${state.pluginDir}` : '';
}

async function loadInstalledConfig(silent = false) {
  try {
    const text = await window.managerApi.readConfig(els.ets2Path.value.trim());
    if (!text) {
      if (!silent) setStatus('插件目录里还没有配置文件');
      return;
    }
    const config = JSON.parse(text);
    els.targetLang.value = config.target_lang || 'zh-CN';
    els.workers.value = config.workers ?? 8;
    els.queueLimit.value = config.queue_limit ?? 1000;
    els.cacheLimit.value = config.cache_limit ?? 1500;
    els.timeoutMs.value = config.timeout_ms ?? 10000;
    els.fontSize.value = config.font_size ?? 18;
    setFontSize(config.font_size ?? 18);
    const primary = (config.providers || []).find((provider) => provider.enabled && !['mymemory', 'andeer'].includes(provider.kind));
    if (primary) {
      const kind = primary.kind || '';
      const baseUrl = primary.base_url || '';
      if (kind === 'anthropic') els.preset.value = 'anthropic';
      else if (kind === 'openai_compatible' || kind === 'openai') els.preset.value = 'openai';
      else if (kind === 'deepl') els.preset.value = 'deepl';
      else if (kind === 'google_cloud' || kind === 'google_translate') els.preset.value = 'google-cloud';
      else if (kind === 'microsoft' || kind === 'azure_translator') els.preset.value = 'microsoft';
      else if (kind === 'baidu') els.preset.value = 'baidu';
      else if (kind === 'youdao') els.preset.value = 'youdao';
      else if (kind === 'tencent' || kind === 'tencent_cloud' || kind === 'tencent_tmt') els.preset.value = 'tencent';
      else if (kind === 'libretranslate') els.preset.value = 'libretranslate';
      else els.preset.value = 'openai';
      els.providerLabel.value = primary.label || (PROVIDER_PRESETS[els.preset.value] || PROVIDER_PRESETS.openai).label;
      els.baseUrl.value = primary.base_url || (PROVIDER_PRESETS[els.preset.value] || PROVIDER_PRESETS.openai).baseUrl;
      els.apiKey.value = primary.api_key || '';
      els.apiSecret.value = primary.api_secret || '';
      els.model.value = primary.model || (PROVIDER_PRESETS[els.preset.value] || PROVIDER_PRESETS.openai).model;
      els.sourceLang.value = primary.source || primary.source_lang || 'auto';
    } else {
      els.preset.value = 'free';
    }
    els.enableFallbacks.checked = (config.providers || []).some((provider) => ['mymemory', 'andeer'].includes(provider.kind));
    els.preview.value = JSON.stringify(config, null, 2);
    updatePreview();
    setStatus('已读取已安装配置');
  } catch (error) {
    setStatus(`读取配置失败：${error.message}`);
  }
}

els.detectBtn.addEventListener('click', async () => {
  els.ets2Path.value = await window.managerApi.detectPath();
  setStatus(els.ets2Path.value ? '已自动识别 ETS2 路径' : '未能自动识别，请手动选择');
  await refreshState();
  await loadInstalledConfig(true);
});

els.browseBtn.addEventListener('click', async () => {
  const selected = await window.managerApi.browsePath();
  if (selected) {
    els.ets2Path.value = selected;
    await refreshState();
    await loadInstalledConfig(true);
  }
});

els.installBtn.addEventListener('click', async () => {
  try {
    await window.managerApi.installDll(els.ets2Path.value.trim());
    setStatus('DLL 已安装/更新');
    await refreshState();
  } catch (error) {
    setStatus(`安装失败：${error.message}`);
  }
});

els.uninstallBtn.addEventListener('click', async () => {
  try {
    await window.managerApi.uninstallDll(els.ets2Path.value.trim());
    setStatus('DLL 已卸载，配置已保留');
    await refreshState();
  } catch (error) {
    setStatus(`卸载失败：${error.message}`);
  }
});

els.loadConfigBtn.addEventListener('click', () => loadInstalledConfig(false));
els.previewBtn.addEventListener('click', updatePreview);

els.saveConfigBtn.addEventListener('click', async () => {
  try {
    JSON.parse(els.preview.value);
    await window.managerApi.writeConfig(els.ets2Path.value.trim(), els.preview.value);
    setStatus('翻译配置已保存');
    await refreshState();
  } catch (error) {
    setStatus(`保存失败：${error.message}`);
  }
});

for (const input of [
  els.preset,
  els.targetLang,
  els.workers,
  els.queueLimit,
  els.cacheLimit,
  els.timeoutMs,
  els.fontSize,
  els.providerLabel,
  els.baseUrl,
  els.apiKey,
  els.apiSecret,
  els.model,
  els.sourceLang,
  els.enableFallbacks
]) {
  input.addEventListener('input', updatePreview);
  input.addEventListener('change', updatePreview);
}

els.preset.addEventListener('change', () => applyPresetDefaults(true));

// --- 字体大小控件 ---
function setFontSize(size) {
  size = Math.max(12, Math.min(28, size));
  els.fontSize.value = size;
  els.fontSizeValue.textContent = size;
  // 高亮对应预设按钮
  document.querySelectorAll('.preset-btn').forEach(btn => {
    btn.classList.toggle('active', Number(btn.dataset.size) === size);
  });
  updatePreview();
}

els.fontSizeMinus.addEventListener('click', () => {
  setFontSize(numberValue(els.fontSize, 18) - 2);
});

els.fontSizePlus.addEventListener('click', () => {
  setFontSize(numberValue(els.fontSize, 18) + 2);
});

document.querySelectorAll('.preset-btn').forEach(btn => {
  btn.addEventListener('click', () => {
    setFontSize(Number(btn.dataset.size));
  });
});

(async function init() {
  els.ets2Path.value = await window.managerApi.detectPath();
  setFontSize(numberValue(els.fontSize, 18));
  applyPresetDefaults(true);
  await refreshState();
  await loadInstalledConfig(true);
})();
