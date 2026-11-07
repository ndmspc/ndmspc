/*
 * Build the recording stages for one tour.
 *
 * The tour JSON drives the parts it describes (open, run, expand, click, anchors). Dialog values
 * (axes, rebin, output file, tab) are prose-only in today's tours, so `overrides.json` supplies the
 * handful of actions those steps need, keyed by `<action>#<tour index>` and step index.
 */
import { readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = dirname(fileURLToPath(import.meta.url));

export function loadOverrides(path = join(HERE, 'overrides.json')) {
  try {
    return JSON.parse(readFileSync(path, 'utf8'));
  } catch {
    return {};
  }
}

const q = (value) => JSON.stringify(String(value));

function actionCode(action) {
  if (typeof action === 'string') return `await ${action};`;
  if (action.clickText !== undefined) return `await N.clickText(${q(action.clickText)}, 600);`;
  if (action.clickTextCI !== undefined) return `await N.clickTextCI(${q(action.clickTextCI)}, 600);`;
  if (action.clickCopy !== undefined) return `await N.clickCopy(${q(action.clickCopy)});`;
  if (action.setFile !== undefined) return `await N.setFile(${q(action.setFile)});`;
  if (action.save !== undefined) return `await N.save(900);`;
  if (action.expand !== undefined) return `await N.expandTree(${q(action.expand)});`;
  if (action.clickTree !== undefined) return `await N.clickTree(${q(action.clickTree)});`;
  if (action.openSparse !== undefined) return `await N.openSparse();`;
  if (action.clickTab !== undefined) {
    return `await N.clickEl(document.querySelector('[data-tour="dialog-tab:${Number(action.clickTab)}"]'), 800);`;
  }
  if (action.clickAnchor !== undefined) {
    return `await N.clickEl(document.querySelector('[data-tour=${q(action.clickAnchor)}]'), 800);`;
  }
  if (action.closeMacro !== undefined) return 'await N.closeMacro();';
  if (action.setAxes !== undefined) return `await N.setAxes(${JSON.stringify(action.setAxes)});`;
  if (action.selectAxes !== undefined) return `await N.selectAxes(${JSON.stringify(action.selectAxes)});`;
  if (action.setNumber !== undefined) {
    const { label, index, value } = action.setNumber;
    return `await N.setNumber(${q(label)}, ${Number(index)}, ${Number(value)});`;
  }
  if (action.chooseOption !== undefined) {
    const [index, text] = action.chooseOption;
    return `await N.chooseOption(${Number(index)}, ${q(text)});`;
  }
  if (action.setOutput !== undefined) {
    const [match, value] = action.setOutput;
    return `await N.setInputByValue(${q(match)}, ${q(value)});`;
  }
  if (action.submit !== undefined) {
    return [
      `await N.clickEl(N.dialogButton(${q(action.submit)}), 800);`,
      'await N.waitFor(() => N.dialog() === null, 8000);',
    ].join('\n      ');
  }
  if (action.waitForText !== undefined) {
    return `await N.waitFor(() => document.body.textContent.includes(${q(action.waitForText)}), 20000);`;
  }
  if (action.waitForSelector !== undefined) {
    return `await N.waitFor(() => document.querySelector(${q(action.waitForSelector)}) !== null, 8000);`;
  }
  if (action.sleep !== undefined) return `await N.sleep(${Number(action.sleep)});`;
  throw new Error(`unknown stage action: ${JSON.stringify(action)}`);
}

function stageCode(actions) {
  const body = actions.map((action) => '      ' + actionCode(action)).join('\n');
  return `(async () => {\n  const N = window.ND;\n${body}\n  return 'ok';\n})()`;
}

function genericActions(step, warnings, index) {
  const actions = [];
  if (step.action !== undefined && step.params !== undefined && step.params.file !== undefined) {
    actions.push({ setFile: step.params.file });
  }
  if (step.show === 'saveAndRun') actions.push({ save: true });
  if (step.expand !== undefined) actions.push({ expand: step.expand });
  if (step.click !== undefined) actions.push({ clickTree: step.click });
  if (typeof step.show === 'string' && step.show.startsWith('dialog-tab:')) {
    actions.push({ clickTab: Number(step.show.slice('dialog-tab:'.length)) });
  }
  if (step.show === 'dialog-preview') actions.push({ clickAnchor: 'dialog-preview' });
  if (step.show === 'notice') actions.push({ waitForText: 'Import finished' });
  if (actions.length === 0 && step.show === 'dialog') {
    warnings.push(
      `step ${index} shows a dialog but its values are prose-only; add them to overrides.json`,
    );
  }
  return actions;
}

/**
 * @returns {{stages: {step: number, code: string, title: string}[], warnings: string[]}}
 */
export function buildStages(tour, { id, overrides = {} }) {
  const steps = Array.isArray(tour.steps) ? tour.steps : [];
  const perTour = overrides[id] ?? {};
  const stages = [];
  const warnings = [];

  steps.forEach((step, index) => {
    const overridden = perTour[String(index)];
    const actions = Array.isArray(overridden)
      ? overridden
      : genericActions(step ?? {}, warnings, index);
    if (actions.length === 0) return;
    stages.push({
      step: index,
      title: step?.show ?? step?.action ?? `step ${index}`,
      code: stageCode(actions),
    });
  });

  return { stages, warnings };
}

export function describeStages(stages) {
  return stages.map((stage, index) => `${index + 1}. step ${stage.step} (${stage.title})`).join('\n');
}
