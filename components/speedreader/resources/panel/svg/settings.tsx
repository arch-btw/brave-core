import * as React from "react"

const SvgComponent = (props: any) => (
  <svg
    xmlns="http://www.w3.org/2000/svg"
    width={14}
    height={10}
    fill="none"
    {...props}
  >
    <path
      fill="currentColor"
      d="M1.003 9.399c.346 0 .543-.168.669-.556l.651-1.853h3.3l.657 1.86c.12.382.317.55.675.55.389 0 .67-.257.67-.61 0-.114-.024-.227-.084-.394L4.91 1.277C4.731.799 4.445.583 3.98.583c-.472 0-.765.222-.938.7L.417 8.395a1.15 1.15 0 0 0-.084.394c0 .365.27.61.67.61Zm1.661-3.503 1.285-3.783h.036l1.297 3.783H2.664Zm7.751 3.514c.82 0 1.62-.442 1.985-1.14h.03v.53c.011.382.257.616.615.616.365 0 .622-.245.622-.652V4.97c0-1.26-.975-2.068-2.498-2.068-1.13 0-2.08.454-2.409 1.166a.883.883 0 0 0-.114.418c0 .323.233.538.556.538a.59.59 0 0 0 .526-.275c.359-.574.771-.795 1.393-.795.788 0 1.26.418 1.26 1.118v.484l-1.726.101c-1.489.084-2.337.76-2.337 1.859 0 1.13.866 1.895 2.097 1.895Zm.353-1.021c-.687 0-1.147-.359-1.147-.909 0-.538.436-.878 1.207-.932l1.554-.096v.496c0 .82-.711 1.44-1.614 1.44Z"
    />
  </svg>
)

export default SvgComponent
