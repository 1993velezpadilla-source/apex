export function el(tag, className = "", attrs = {}) {
  const node = document.createElement(tag);
  if (className) node.className = className;

  for (const [key, value] of Object.entries(attrs)) {
    if (key === "text") node.textContent = value;
    else if (key === "style") Object.assign(node.style, value);
    else node.setAttribute(key, value);
  }

  return node;
}
