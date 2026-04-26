/** @type {import('next').NextConfig} */
const nextConfig = {
  output: "standalone",
  reactStrictMode: true,
  eslint: { ignoreDuringBuilds: true },
  // @node-rs/argon2 embarque un binaire natif .node : ne pas le bundler via webpack.
  serverExternalPackages: ['@node-rs/argon2'],
  webpack: (config, { isServer }) => {
    if (isServer) {
      // Fallback explicite : marque @node-rs/argon2 comme module CommonJS externe
      // quelle que soit la version de Next.js (14.x vs 15.x).
      const prev = Array.isArray(config.externals) ? config.externals : [config.externals].filter(Boolean)
      config.externals = [...prev, { '@node-rs/argon2': 'commonjs @node-rs/argon2' }]
    }
    return config
  },
};

export default nextConfig;
