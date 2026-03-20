import { useState, useEffect, useCallback, useRef } from 'react';
import * as Juce from 'juce-framework-frontend';
import { normalizeLoadedState, createEmptyPattern } from '../utils/helpers';
import { PATTERN_LABELS } from '../utils/constants';

const REQUIRED_NATIVE_FUNCTIONS = [
    'updateCPlusPlusState',
    'saveFullUiState',
    'requestInitialState',
    'uiReadyForEngineState'
];

const NATIVE_CALL_TIMEOUT_MS = {
    requestInitialState: 3000,
    uiReadyForEngineState: 1500,
    updateCPlusPlusState: 1200,
    saveFullUiState: 1200,
};

const ENGINE_SYNC_DELAY_MS = 75;
const UI_ONLY_SAVE_DELAY_MS = 300;
const PATTERN_SAVE_DELAY_MS = 2500;
const SAVE_RETRY_DELAY_MS = 250;

const wait = (ms) => new Promise(resolve => window.setTimeout(resolve, ms));

export function useJuceBridge() {
    const [patterns, setPatterns] = useState(PATTERN_LABELS.map(l => createEmptyPattern(`Pattern ${l}`)));
    const [activeIdx, setActiveIdx] = useState(0);
    const [currentStep, setCurrentStep] = useState(0);
    const [isPlaying, setIsPlaying] = useState(false);
    const [bpm, setBpm] = useState(120);
    const [activeSection, setActiveSection] = useState(-1);
    const [currentPage, setCurrentPage] = useState(0);
    const [selectedTrack, setSelectedTrack] = useState(0);
    const [footerTab, setFooterTab] = useState('Velocity');
    const [themeIdx, setThemeIdx] = useState(0);

    const [backendReady, setBackendReady] = useState(false);
    const [uiReady, setUiReady] = useState(false);
    const [backendStatus, setBackendStatus] = useState('Initializing JUCE bridge...');
    const [debugInfo, setDebugInfo] = useState('Waiting for window.__JUCE__...');

    const hasHydrated = useRef(false);
    const syncTimeout = useRef(null);
    const saveUiTimeout = useRef(null);

    const nativeFnsRef = useRef({
        updateCPlusPlusState: null,
        saveFullUiState: null,
        requestInitialState: null,
        uiReadyForEngineState: null,
    });

    const saveInFlightRef = useRef(false);
    const pendingSnapshotJsonRef = useRef(null);
    const lastSavedSnapshotJsonRef = useRef('');
    const lastEnginePayloadRef = useRef('');

    const backendSupportsEvents = useCallback(() => {
        const backend = window.__JUCE__?.backend;
        return !!backend
            && typeof backend.addEventListener === 'function'
            && typeof backend.removeEventListener === 'function';
    }, []);

    const resolveNativeFunctions = useCallback(() => {
        const resolved = {};

        for (const name of REQUIRED_NATIVE_FUNCTIONS) {
            const fn = Juce.getNativeFunction(name);
            if (typeof fn !== 'function') {
                throw new Error(`Juce.getNativeFunction('${name}') did not return a function`);
            }
            resolved[name] = fn;
        }

        nativeFnsRef.current = resolved;
        return resolved;
    }, []);

    const getNativeFn = useCallback((name) => {
        const fn = nativeFnsRef.current[name];
        if (typeof fn !== 'function') {
            throw new Error(`Native function '${name}' is not ready yet`);
        }
        return fn;
    }, []);

    const invokeNativeWithTimeout = useCallback(async (name, args = [], timeoutMs = NATIVE_CALL_TIMEOUT_MS[name] ?? 1500) => {
        const nativeFn = getNativeFn(name);
        const invocation = Promise.resolve().then(() => nativeFn(...args));

        if (timeoutMs <= 0) {
            return await invocation;
        }

        let timeoutId = 0;
        const timeoutPromise = new Promise((_, reject) => {
            timeoutId = window.setTimeout(() => {
                reject(new Error(`${name} timed out after ${timeoutMs}ms`));
            }, timeoutMs);
        });

        try {
            return await Promise.race([invocation, timeoutPromise]);
        } finally {
            if (timeoutId) {
                window.clearTimeout(timeoutId);
            }
        }
    }, [getNativeFn]);

    const buildUiSnapshot = useCallback((patternState = patterns, overrides = {}) => ({
        patterns: patternState,
        activeIdx: overrides.activeIdx ?? activeIdx,
        themeIdx: overrides.themeIdx ?? themeIdx,
        selectedTrack: overrides.selectedTrack ?? selectedTrack,
        currentPage: overrides.currentPage ?? currentPage,
        footerTab: overrides.footerTab ?? footerTab
    }), [patterns, activeIdx, themeIdx, selectedTrack, currentPage, footerTab]);

    const flushQueuedSnapshotSave = useCallback(async () => {
        if (!backendReady || !hasHydrated.current) return;
        if (saveInFlightRef.current) return;

        const snapshotJson = pendingSnapshotJsonRef.current;
        if (!snapshotJson || snapshotJson === lastSavedSnapshotJsonRef.current) return;

        pendingSnapshotJsonRef.current = null;
        saveInFlightRef.current = true;

        try {
            await invokeNativeWithTimeout('saveFullUiState', [snapshotJson], NATIVE_CALL_TIMEOUT_MS.saveFullUiState);
            lastSavedSnapshotJsonRef.current = snapshotJson;
        } catch (err) {
            console.error('saveFullUiState failed', err);

            if (!pendingSnapshotJsonRef.current) {
                pendingSnapshotJsonRef.current = snapshotJson;
            }
        } finally {
            saveInFlightRef.current = false;

            if (
                pendingSnapshotJsonRef.current &&
                pendingSnapshotJsonRef.current !== lastSavedSnapshotJsonRef.current
            ) {
                if (saveUiTimeout.current) {
                    window.clearTimeout(saveUiTimeout.current);
                }

                saveUiTimeout.current = window.setTimeout(() => {
                    flushQueuedSnapshotSave();
                }, SAVE_RETRY_DELAY_MS);
            }
        }
    }, [backendReady, invokeNativeWithTimeout]);

    const queueUiSnapshotSave = useCallback((snapshot, delayMs) => {
        if (!backendReady || !hasHydrated.current) return;

        const snapshotJson = JSON.stringify(snapshot);

        if (
            snapshotJson === lastSavedSnapshotJsonRef.current ||
            snapshotJson === pendingSnapshotJsonRef.current
        ) {
            return;
        }

        pendingSnapshotJsonRef.current = snapshotJson;

        if (saveUiTimeout.current) {
            window.clearTimeout(saveUiTimeout.current);
        }

        saveUiTimeout.current = window.setTimeout(() => {
            flushQueuedSnapshotSave();
        }, delayMs);
    }, [backendReady, flushQueuedSnapshotSave]);

    const syncPatternToEngine = useCallback((patternData, overrides = {}) => {
        if (!backendReady) return;

        const payload = JSON.stringify({
            ...patternData,
            activePatternIndex: overrides.activeIdx ?? activeIdx,
            selectedTrack: overrides.selectedTrack ?? selectedTrack,
            currentPage: overrides.currentPage ?? currentPage
        });

        if (payload === lastEnginePayloadRef.current) {
            return;
        }

        lastEnginePayloadRef.current = payload;

        invokeNativeWithTimeout('updateCPlusPlusState', [payload], NATIVE_CALL_TIMEOUT_MS.updateCPlusPlusState)
            .catch(err => {
                console.error('updateCPlusPlusState failed', err);

                if (lastEnginePayloadRef.current === payload) {
                    lastEnginePayloadRef.current = '';
                }
            });
    }, [backendReady, activeIdx, selectedTrack, currentPage, invokeNativeWithTimeout]);

    useEffect(() => {
        let cancelled = false;

        const initBackend = async () => {
            try {
                let retries = 120;

                while (!window.__JUCE__?.backend && retries > 0) {
                    if (!cancelled) {
                        setBackendStatus('Waiting for window.__JUCE__.backend...');
                        setDebugInfo('JUCE backend container not injected yet.');
                    }
                    await wait(50);
                    retries--;
                }

                if (!window.__JUCE__?.backend) {
                    if (!cancelled) {
                        setBackendStatus('Error: JUCE backend container never appeared.');
                        setDebugInfo('window.__JUCE__.backend is missing.');
                    }
                    return;
                }

                if (window.__JUCE__?.initialisationPromise) {
                    if (!cancelled) setBackendStatus('Awaiting JUCE initialisationPromise...');
                    await window.__JUCE__.initialisationPromise;
                }

                retries = 120;
                while (retries > 0) {
                    try {
                        resolveNativeFunctions();

                        if (!backendSupportsEvents()) {
                            throw new Error('JUCE backend event API is unavailable');
                        }

                        if (!cancelled) {
                            setBackendReady(true);
                            setBackendStatus('JUCE bridge ready. Starting hydration...');
                            setDebugInfo(`Resolved native functions via Juce.getNativeFunction: ${REQUIRED_NATIVE_FUNCTIONS.join(', ')}`);
                        }
                        return;
                    } catch (err) {
                        if (!cancelled) {
                            setBackendStatus('Waiting for JUCE native functions...');
                            setDebugInfo(err.message);
                        }
                        await wait(50);
                        retries--;
                    }
                }

                if (!cancelled) {
                    setBackendStatus('Error: JUCE native functions never became available.');
                    setDebugInfo('Juce.getNativeFunction could not resolve the required bridge functions.');
                }
            } catch (err) {
                if (!cancelled) {
                    setBackendStatus(`Init failed: ${err.message}`);
                    setDebugInfo(err.stack || String(err));
                }
            }
        };

        initBackend();

        return () => {
            cancelled = true;
        };
    }, [backendSupportsEvents, resolveNativeFunctions]);

    useEffect(() => {
        if (!backendReady || !backendSupportsEvents()) return undefined;

        const handlePlaybackState = (event) => {
            if (event?.bpm) setBpm(Math.round(event.bpm));
            setIsPlaying(!!event?.isPlaying);
            setCurrentStep((event?.isPlaying && event?.currentStep >= 0) ? event.currentStep : 0);
        };

        const listenerHandle = window.__JUCE__.backend.addEventListener('playbackState', handlePlaybackState);
        return () => window.__JUCE__.backend.removeEventListener(listenerHandle);
    }, [backendReady, backendSupportsEvents]);

    useEffect(() => {
        if (!backendReady || !backendSupportsEvents()) return undefined;

        const handleEngineState = (event) => {
            if (!hasHydrated.current || !event?.patternData) return;

            const enginePatternIndex = Number.isInteger(event.activePatternIndex)
                ? event.activePatternIndex
                : activeIdx;

            setPatterns(prev => {
                const next = [...prev];
                if (!next[enginePatternIndex]) return prev;
                next[enginePatternIndex] = { ...next[enginePatternIndex], data: event.patternData };
                return next;
            });

            if (Number.isInteger(event.currentInstrument)) setSelectedTrack(event.currentInstrument);
            if (Number.isInteger(event.currentPage)) {
                setCurrentPage(event.currentPage);
                setActiveSection(event.currentPage);
            }
        };

        const listenerHandle = window.__JUCE__.backend.addEventListener('engineState', handleEngineState);
        return () => window.__JUCE__.backend.removeEventListener(listenerHandle);
    }, [backendReady, backendSupportsEvents, activeIdx]);

    useEffect(() => {
        if (!backendReady || hasHydrated.current) return;

        let cancelled = false;

        const hydrate = async () => {
            let loadedFromBackend = false;
            let normalized = normalizeLoadedState(null);
            let readyNote = 'JUCE ready signal acknowledged.';

            try {
                setBackendStatus('Calling requestInitialState...');
                setDebugInfo('Awaiting initial UI snapshot from JUCE.');

                const savedState = await invokeNativeWithTimeout('requestInitialState');
                loadedFromBackend = true;

                if (cancelled) return;

                setBackendStatus('Initial state received. Applying snapshot...');

                const parsedState = typeof savedState === 'string'
                    ? (() => {
                        try {
                            return JSON.parse(savedState);
                        } catch (err) {
                            console.warn('requestInitialState returned non-JSON string', err);
                            return savedState;
                        }
                    })()
                    : savedState;

                normalized = normalizeLoadedState(parsedState);
            } catch (err) {
                console.error('requestInitialState failed or timed out', err);
                if (!cancelled) {
                    setBackendStatus('Initial state unavailable. Falling back to defaults...');
                    setDebugInfo(err.stack || String(err));
                }
            }

            if (cancelled) return;

            setPatterns(normalized.patterns);
            setActiveIdx(normalized.activeIdx);
            setThemeIdx(normalized.themeIdx);
            setSelectedTrack(normalized.selectedTrack);
            setCurrentPage(normalized.currentPage);
            setActiveSection(normalized.currentPage);
            setFooterTab(normalized.footerTab);

            lastSavedSnapshotJsonRef.current = JSON.stringify({
                patterns: normalized.patterns,
                activeIdx: normalized.activeIdx,
                themeIdx: normalized.themeIdx,
                selectedTrack: normalized.selectedTrack,
                currentPage: normalized.currentPage,
                footerTab: normalized.footerTab
            });

            await new Promise(resolve => window.requestAnimationFrame(() => resolve()));
            if (cancelled) return;

            setBackendStatus('Calling uiReadyForEngineState...');
            setDebugInfo(loadedFromBackend
                ? 'Initial state applied. Notifying JUCE that the UI is live.'
                : 'Using default UI state. Notifying JUCE that the UI is live.');

            try {
                await invokeNativeWithTimeout('uiReadyForEngineState');
            } catch (err) {
                console.warn('uiReadyForEngineState failed or timed out', err);
                readyNote = `UI rendered, but uiReadyForEngineState did not confirm in time: ${err.message}`;
            }

            if (cancelled) return;

            hasHydrated.current = true;
            setUiReady(true);
            setBackendStatus('JUCE ready. UI hydrated.');
            setDebugInfo(loadedFromBackend
                ? `Initial state loaded. ${readyNote}`
                : `Fell back to default UI state. ${readyNote}`);
        };

        hydrate().catch((err) => {
            console.error('JUCE hydration failed', err);
            if (!cancelled) {
                hasHydrated.current = true;
                setUiReady(true);
                setBackendStatus('Hydration failed. Continuing with defaults.');
                setDebugInfo(err.stack || String(err));
            }
        });

        return () => {
            cancelled = true;
        };
    }, [backendReady, invokeNativeWithTimeout]);

    useEffect(() => {
        if (!hasHydrated.current || !backendReady) return;
        queueUiSnapshotSave(buildUiSnapshot(), PATTERN_SAVE_DELAY_MS);
    }, [patterns, backendReady, buildUiSnapshot, queueUiSnapshotSave]);

    useEffect(() => {
        if (!hasHydrated.current || !backendReady) return;
        queueUiSnapshotSave(buildUiSnapshot(), UI_ONLY_SAVE_DELAY_MS);
    }, [activeIdx, themeIdx, selectedTrack, currentPage, footerTab, backendReady, buildUiSnapshot, queueUiSnapshotSave]);

    useEffect(() => {
        return () => {
            if (syncTimeout.current) {
                window.clearTimeout(syncTimeout.current);
            }
            if (saveUiTimeout.current) {
                window.clearTimeout(saveUiTimeout.current);
            }
        };
    }, []);

    const updateUiAndEngine = useCallback((newData) => {
        const currentPattern = patterns[activeIdx];
        if (!currentPattern) return;

        const updatedData = { ...currentPattern.data, ...newData };

        setPatterns(prev => {
            const next = [...prev];
            next[activeIdx] = { ...next[activeIdx], data: updatedData };
            return next;
        });

        if (syncTimeout.current) window.clearTimeout(syncTimeout.current);
        syncTimeout.current = window.setTimeout(() => {
            syncPatternToEngine(updatedData);
        }, ENGINE_SYNC_DELAY_MS);
    }, [activeIdx, patterns, syncPatternToEngine]);

    return {
        patterns,
        activeIdx,
        currentStep,
        isPlaying,
        bpm,
        activeSection,
        currentPage,
        selectedTrack,
        footerTab,
        themeIdx,
        setActiveIdx,
        setActiveSection,
        setCurrentPage,
        setSelectedTrack,
        setFooterTab,
        setThemeIdx,
        updateUiAndEngine,
        syncPatternToEngine,
        backendReady,
        uiReady,
        backendStatus,
        debugInfo,
        hasHydrated,
    };
}