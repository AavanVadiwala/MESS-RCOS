// Circle that fills once per poll interval. Pair it with a setInterval of the
// same length; change `key` (or remount) to restart it when the poll restarts.
const R = 9;
const C = 2 * Math.PI * R;

function RefreshRing({ intervalMs }) {
  return (
    <svg className="refresh-ring" width="24" height="24" viewBox="0 0 24 24" aria-label="Time until next refresh">
      <circle className="ring-track" cx="12" cy="12" r={R} />
      <circle
        className="ring-fill"
        cx="12"
        cy="12"
        r={R}
        style={{ "--ring-len": C, animationDuration: `${intervalMs}ms` }}
      />
    </svg>
  );
}

export default RefreshRing;
