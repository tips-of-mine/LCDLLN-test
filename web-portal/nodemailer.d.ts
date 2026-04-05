/** Ambiant : garantit le typage au build Docker même si @types/nodemailer n’est pas résolu. */
declare module "nodemailer" {
  export function createTransport(options: Record<string, unknown>): {
    sendMail(mail: Record<string, unknown>): Promise<unknown>;
  };
}
